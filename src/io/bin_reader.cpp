#include "bin_reader.hpp"

/**
 * @brief 以二进制模式打开文件，校验打开结果
 * @param fp 输出参数，文件句柄引用；打开成功后写入有效的FILE指针
 * @param path 待读取的二进制文件路径
 * @note 打开失败时自动打印错误信息到stderr
 * @return true 文件打开成功；false 文件打开失败
 */
static bool fileop_check(FILE *&fp,const char *path)
{
    //以二进制打开
    fp = fopen(path,"rb");
    if (fp == nullptr)
    {
        // fopen失败时，会自动设置errno
        fprintf(stderr, "[ERROR] fopen failed, path: %s, errno: %d, msg: %s\n",
                path, errno, strerror(errno));
        return false;
    }
    return true;
}

bool err_check(int err,const char* filename, int lineNumber)
{
    if(err < 0)
    {
        printf("error:\nfile=%s,line=%d\n",
            filename,
            lineNumber);
            return false;
    }
    return true;
}


/**
 * @brief 读取bin文件大小，按单帧字节长度计算【完整帧】数量
 * @param path 待读取的二进制文件路径
 * @note 向下取整（截断）：不足一帧的尾部丢弃、不计入帧数，并打 WARN 提示字节数。
 *       空文件或仅含 8 字节文件头时返回 0，不报错。
 * @return 正常：完整帧数（int64）；失败：返回 -1（fopen/fseeko/ftello 错误）
 */
int64_t bin_frame_count(const char *path)
{
    FILE *fp = nullptr;
    if(!fileop_check(fp,path))
        return -1;
    if(0>fseeko(fp,0,SEEK_END))
    {
        fprintf(stderr, "[ERROR] fseeko failed, path: %s, errno: %d, msg: %s\n",
                path, errno, strerror(errno));
        fclose(fp);
        return -1;
        
    }
    off_t file_bytes = ftello(fp);//ftello返回off_t,恒定8字节
    if (0>file_bytes)
    {
        fprintf(stderr, "[ERROR] ftell failed, path: %s, errno: %d, msg: %s\n",
                path, errno, strerror(errno));
        fclose(fp);
        return -1;
    }
    fclose(fp);
    int64_t span = (int64_t)file_bytes - FILE_HDR_BYTES;
    if (span > 0 && span % FRAME_BYTES != 0)
    {
        fprintf(stderr, "[WARN] %s 尾部有 %lld 字节不足一帧，已丢弃\n",
                path, (long long)(span % FRAME_BYTES));
    }
    return span / FRAME_BYTES;
}

/**
 * @brief 读取指定序号帧的帧头（72字节）
 * @param path bin文件路径
 * @param idx 帧序号，0起始
 * @param out72 输出缓冲区，至少72字节空间
 * @note 跳过文件全局头定位目标帧；半包帧头返回-1；IO错误返回-1
 * @return 0 读取成功；-1 IO失败/半包
 */
int bin_read_frame_header(const char *path, int64_t idx, uint8_t *out72)
{
    FILE *fp = nullptr;
    if(!fileop_check(fp,path))
        return -1;
    off_t idbytes = FILE_HDR_BYTES+(off_t)idx*FRAME_BYTES;
    if(0>fseeko(fp,idbytes,SEEK_SET))
    {
        fprintf(stderr, "[ERROR] fseeko failed, path: %s, errno: %d, msg: %s\n",
                path, errno, strerror(errno));
        fclose(fp);
        return -1;
    }
    size_t read_bytes = fread(out72,1,FRAME_HDR_BYTES,fp);
    //读取帧头时，读到末尾的情况正常不会出现
    if(read_bytes<FRAME_HDR_BYTES)
    {
        if(ferror(fp))
        {
            fprintf(stderr, "[ERROR] fread failed, path: %s, errno: %d, msg: %s\n",
                path, errno, strerror(errno));
            fclose(fp);
            return -1;
        }
        else 
        {
            fclose(fp);
            fprintf(stderr, "[WARN]剩余数据不足帧头72bytes，共读取%zu个字节\n",read_bytes);
            return -1;  
        }
    }
    fclose(fp);
    return 0;
}

/**
 * @brief 读取指定序号帧的数据部分
 * @param path bin文件路径
 * @param idx 帧序号，0起始
 * @param out_cs 输出缓冲区，需容纳 CHAN_NUM*FRM_NUM 个 float（= 32768 字节）
 * @note 跳过全局头、跳过目标帧的帧头，读取帧内采样数据；半包返回-1；IO错误返回-1
 * @return 0 读取成功；-1 IO失败/半包
 */
int bin_read_frame_data(const char *path, int64_t idx, float *out_cs)
{
    FILE *fp = nullptr;
    if(!fileop_check(fp,path))
        return -1;
    off_t idbytes = FILE_HDR_BYTES+(off_t)idx*FRAME_BYTES+FRAME_HDR_BYTES;
    if(0>fseeko(fp,idbytes,SEEK_SET))
    {
        fprintf(stderr, "[ERROR] fseeko failed, path: %s, errno: %d, msg: %s\n",
                path, errno, strerror(errno));
        fclose(fp);
        return -1;
    }
    size_t read_bytes = fread(out_cs,1,FRAME_BYTES_NOHEAD,fp);
    if(read_bytes<FRAME_BYTES_NOHEAD)
    {
        if(ferror(fp))
        {
            fprintf(stderr, "[ERROR] fread failed, path: %s, errno: %d, msg: %s\n",
                path, errno, strerror(errno));
            fclose(fp);
            return -1;
        }
        else 
        {
            fclose(fp);
            fprintf(stderr, "[WARN]剩余数据不足一帧（256×32×4）bytes，共读取%zu个字节\n",read_bytes);
            return -1;  
        }
    }
    fclose(fp);
    return 0;
}


int bin_read_time(const char *path, int64_t idx, string& buf)
{
    FILE *fp = nullptr;
    if(!fileop_check(fp,path))
        return -1;
    off_t idbytes = FILE_HDR_BYTES + (off_t)idx*FRAME_BYTES + FRAME_HDR_BYTES_FNON;
    if(0>fseeko(fp,idbytes,SEEK_SET))
    {
        fprintf(stderr, "[ERROR] fseeko failed, path: %s, errno: %d, msg: %s\n",
                path, errno, strerror(errno));
        fclose(fp);
        return -1;
    }
    //读取日期
    uint32_t datatime[3];
    size_t read_bytes = fread(datatime,4,3,fp);
    if(read_bytes<3)
    {
        if(ferror(fp))
        {
            fprintf(stderr, "[ERROR] fread failed, path: %s, errno: %d, msg: %s\n",
                path, errno, strerror(errno));
            fclose(fp);
            return -1;
        }
        else 
        {
            fclose(fp);
            fprintf(stderr, "[WARN]剩余数据不足4字节（uint_32）bytes，共读取%zu个字节\n",read_bytes);
            return -1;  
        }
    }
    /*提取出来的data数据为0xYYYY MM DD,即0x20261107
    要返回十进制20261107*/
    datetime typ;
    typ = Date;
    uint64_t data_dec = bcd_date_to_integer(datatime[0],typ);
    
    //读取时间
    typ = Time_intuitive;
    uint32_t time = bcd_date_to_integer(datatime[1],typ);
    
    //读取纳秒
    uint32_t time_ns = datatime[2];

    /* 日期 YYYYMMDD / 时间 HHMMSS 都是【十进制】数，只能用 / 和 % 拆位；
       用 >> 位移拆的是二进制位，和十进制位无关 */
    int year   = (int)(data_dec / 10000);
    int month  = (int)((data_dec / 100) % 100);
    int day    = (int)(data_dec % 100);

    int hour   = (int)(time / 10000);
    int minute = (int)((time / 100) % 100);
    int second = (int)(time % 100);

    int msec = (int)(time_ns / 1000000);   /* 纳秒 -> 毫秒 3 位 */

    char s[64];
    snprintf(s, sizeof s, "%04d-%02d-%02d %02d:%02d:%02d.%03d",
             year, month, day, hour, minute, second, msec);
    buf = s;
 
    return 0;
}



/**
 * @brief 读取首文件0号帧时间戳，计算未扣除Kmax的周期理想起点帧号
 * @param path 首bin文件完整路径
 * @param cycle_sec 周期长度(秒)，传入 WORK_PERIOD
 * @param ffftime 输出时间锚点结构体
 * @param ffftime.first_frame_idx 理想起点帧号，文件内0基，未扣Kmax
 * @param ffftime.first_sec 周期名义起点，当日累计秒，跨天持续累加
 * @note 判定语义：严格大于；首帧秒刚好对齐周期刻度时，自动取下一个刻度
 * @note 对齐MATLAB Main_LFM_AR.m L292-L295逻辑：temp1 = segLen - mod(SS,segLen)
 * @note Kmax回退、最终取整逻辑交由外部函数处理，本函数仅输出理想值
 * @return 0 成功；-1 失败（参数非法/读时间失败/时间解析失败/起点帧越界）
 * @example bin_find_start_frame("/home/waster/data/sim/SIM_00001.bin", WORK_PERIOD, anchor);
 */
int bin_find_start_frame(const char *path, int cycle_sec, FFFTimeAnchor &ffftime)
{
    if (cycle_sec <= 0)
    {
        fprintf(stderr, "[ERROR] cycle_sec 必须为正，当前 %d\n", cycle_sec);
        return -1;
    }

    string ff_time;
    if (bin_read_time(path, 0, ff_time) != 0)
        return -1;

    int hour = 0, min = 0, sec = 0, msec = 0;
    if (sscanf(ff_time.c_str(), "%*[^ ] %02d:%02d:%02d.%03d",
               &hour, &min, &sec, &msec) != 4)
    {
        fprintf(stderr, "[ERROR] 首帧时间格式无法解析：%s\n", ff_time.c_str());
        return -1;
    }

    /* 首帧整数秒：周期刻度必落在整秒上，毫秒只影响帧号，不影响刻度 */
    const int64_t first_sec_of_day = (int64_t)hour * 3600 + min * 60 + sec;

    /* 严格大于语义：sec % cycle_sec == 0 时，to_boundary_s 仍等于 cycle_sec */
    const int64_t to_boundary_s = cycle_sec - (sec % cycle_sec);

    /* 到下一个刻度的毫秒数，再按 FRAME_MS（=1 ms）折算成帧号 */
    const int64_t dist_ms = to_boundary_s * 1000 - msec;
    const int64_t ideal_idx = (dist_ms + FRAME_MS - 1) / FRAME_MS;   /* 向上取整 */

    const int64_t binFrm = bin_frame_count(path);
    if (binFrm < 0)
        return -1;
    if (ideal_idx >= binFrm)
    {
        fprintf(stderr, "[ERROR] 起点越界：需要第 %lld 帧，文件仅 %lld 帧\n",
                (long long)ideal_idx, (long long)binFrm);
        return -1;
    }

    ffftime.first_frame_idx = ideal_idx;
    ffftime.first_sec = (uint32_t)(first_sec_of_day + to_boundary_s);
    return 0;
}

/**
 * @brief 基于Kmax向前回退起点帧索引，修正时间锚点
 * @param ffftime 输入输出：时间锚点结构体，first_frame_idx 会被向前偏移
 * @note 回退量 = Kmax / FRM_NUM；注意首文件开头不足一个回退量,钳到第0帧
 * @return 0 成功
 */
int bin_Kmax_start_frame(FFFTimeAnchor &ffftime)
{

    ffftime.first_Kmax_idx = ffftime.first_frame_idx-Kmax_OFFSET;

    if (ffftime.first_Kmax_idx < 0)
    {
        // 首文件开头不足一个回退量：MATLAB 的 find 会落到第 1 帧，这里同样钳到 0 帧
        ffftime.first_Kmax_idx = 0;
        fprintf(stderr, "[WARN] 首文件开头不足 %lld ms 保护带，已钳到第 0 帧\n",
                (long long)(Kmax * 1000 / FS));
        return 1;      // 0 = 正常回退；1 = 保护带不足已钳位
    }
    return 0;
}

/**
 * @brief 由首周期锚点（已扣 Kmax）推算全部周期起点，依次追加到 cycle_anchor_list
 * @param anchor 首周期锚点；first_frame_idx 未扣 Kmax，first_Kmax_idx 已扣 Kmax
 * @param cycle_anchor_list 输出表，调用方传入空 vector
 * @return 成功：周期总数；失败：-1
 * @note 步进固定 = WORK_FRAME_CNT（30000 帧 = 30 s），窗口长度 = 30051 帧
 * @note 跨文件依靠 pad 衔接：本文件剩余帧不足一个步进时，下一周期起点 = 下一文件第 (WORK_FRAME_CNT - 本文件剩余) 帧
 * @note 起点 + 30051 超过数据总帧数的周期直接丢弃，末周期读不满，和 MATLAB 逻辑对齐
 */
int bin_glob_frame(FFFTimeAnchor anchor, vector<CycleAnchor> &cycle_anchor_list)
{
    const int64_t period_frames = WORK_PERIOD_ALIGN_CNT / FRM_NUM;   /* 一周期帧数 = 30051 */
    const int     nfiles        = FILE_END - FILE_START + 1;

    /* 先把每个文件的帧数读一遍：既得到总帧数，也避免后面循环里重复 stat */
    vector<int64_t> fcnt(nfiles, 0);
    int64_t total_frames = 0;
    for (int i = 0; i < nfiles; ++i)
    {
        char path_buf[256];
        if (!Check(snprintf(path_buf, sizeof(path_buf), FILE_PATH, FILE_START + i)))
            return -1;
        if (!Check(fcnt[i] = bin_frame_count(path_buf)))
            return -1;
        total_frames += fcnt[i];
    }

    /* 连首周期都读不满，直接失败 */
    if (anchor.first_Kmax_idx + period_frames > total_frames)
    {
        fprintf(stderr, "[ERROR] 数据不足一个周期：需要 %lld 帧，实际 %lld 帧\n",
                (long long)period_frames, (long long)total_frames);
        return -1;
    }

    /* 首周期：直接由锚点生成 */
    CycleAnchor first{};
    first.cycle_id          = 1;
    first.file_id           = FILE_START;
    first.file_frame        = anchor.first_frame_idx;
    first.global_frame      = anchor.first_frame_idx;
    first.global_kmax_frame = anchor.first_Kmax_idx;
    first.enable_frame_pad  = false;
    first.pad_frame_cnt     = 0;
    cycle_anchor_list.emplace_back(first);

    for (int i = 0; i < nfiles; ++i)
    {
        const int     file_id     = FILE_START + i;
        const int64_t frame_count = fcnt[i];

        /* 本文件内第一个待生成周期的帧号：
         *   上一周期标了 pad（本文件不够步进）→ 用 pad 给出的落点
         *   否则沿上一周期再走一个步进 */
        int64_t next_frame = cycle_anchor_list.back().enable_frame_pad
                             ? cycle_anchor_list.back().pad_frame_cnt
                             : cycle_anchor_list.back().file_frame + WORK_FRAME_CNT;

        while (next_frame < frame_count)
        {
            const int64_t next_global = cycle_anchor_list.back().global_frame + WORK_FRAME_CNT;

            /* 这一周期要从 next_global-25 起读满 30051 帧，数据不够就到此为止 */
            if (next_global - Kmax_OFFSET + period_frames > total_frames)
                return static_cast<int>(cycle_anchor_list.size());

            CycleAnchor cur{};
            cur.cycle_id          = cycle_anchor_list.back().cycle_id + 1;
            cur.global_frame      = next_global;
            cur.global_kmax_frame = next_global - Kmax_OFFSET;
            cur.file_id           = file_id;
            cur.file_frame        = next_frame;

            /* 本文件剩下的帧不足一个步进 → 下一个周期落到下一文件 */
            const int64_t left = frame_count - next_frame;
            if (left < WORK_FRAME_CNT)
            {
                cur.enable_frame_pad = true;
                cur.pad_frame_cnt    = WORK_FRAME_CNT - left;
            }

            cycle_anchor_list.emplace_back(cur);
            next_frame += WORK_FRAME_CNT;
        }
    }

    return static_cast<int>(cycle_anchor_list.size());
}
