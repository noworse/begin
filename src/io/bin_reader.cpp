/******************************************************************************
 * 文件：bin_reader.cpp
 * 作用：bin 文件的读取、时间戳解析、周期起点定位。
 * 分区：
 *   一、内部工具     fileop_check / err_check
 *   二、基础读取     帧数、帧头、帧数据体
 *   三、时间戳解析   帧头 date/time/ns → 字符串
 *   四、周期定位     理想起点 → 扣 Kmax → 全部周期
 *   五、全局周期推算 首周期 → 跨文件推算全部周期
 *   六、周期写盘     整周期 float 数组 → 文件（与 MATLAB 逐位对拍）
 * 约定：帧号一律 0-based；失败统一返回 -1（错误信息写 stderr）。
 ******************************************************************************/

#include "bin_reader.hpp"

/**************************一、内部工具*************************/

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

/**
 * @brief 统一错误检查：err < 0 判为失败，并打印出错的文件名与行号
 * @param err 被检查的返回值（约定负数代表失败）
 * @param filename 出错文件名（由 Check 宏用 __FILE__ 自动填入）
 * @param lineNumber 出错行号（由 Check 宏用 __LINE__ 自动填入）
 * @return true 没有出错；false 出错
 * @note 标准用法：if(!Check(调用)) return -1;
 */
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


/**************************二、基础读取*************************/

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


/**************************三、时间戳解析*************************/

/*
 * 函数名：bin_read_time
 * 描述  ：读第 idx 帧帧头里的 date/time/ns，格式化成 "YYYY-MM-DD HH:MM:SS.mmm"
 * 输入  ：- path bin 文件路径
 *         - idx  帧号，0-based
 * 输出  ：- buf  格式化结果写入此处（固定 23 字符，另加结尾 '\0'）
 * 返回  ：0 成功；-1 失败（打不开 / 定位失败 / 帧头读不满 12 字节）
 * 调用  ：bin_read_time(path, 0, s);
 * 说明  ：date/time 是 BCD 打包数，解码交给 bcd_date_to_integer；
 *         ns 只取毫秒部分，输出格式与宏 DATATIME_EXAMPLE 一一对应
 */
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



/**************************四、周期定位*************************/

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

/**************************五、全局周期推算*************************/

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
    first.file_kmax_frame   = anchor.first_Kmax_idx;
    first.global_frame      = anchor.first_frame_idx;
    first.global_kmax_frame = anchor.first_Kmax_idx;
    first.enable_frame_pad  = false;
    first.pad_frame_cnt     = 0;
    cycle_anchor_list.emplace_back(first);

    for (int i = 0; i < nfiles; ++i)
    {
        const int     file_id     = FILE_START + i;
        const int64_t frame_count = fcnt[i];

        /* 数据断裂检查：上一周期要求本文件从 pad 处接上，但本文件没这么长，
         * 说明数据中间缺了一段，继续算会把周期错位，直接报错 */
        if (cycle_anchor_list.back().enable_frame_pad &&
            cycle_anchor_list.back().pad_frame_cnt >= frame_count)
        {
            fprintf(stderr,
                    "[ERROR] 文件 %d 只有 %lld 帧，不足以衔接上一周期（需从第 %lld 帧起）\n",
                    file_id, (long long)frame_count,
                    (long long)cycle_anchor_list.back().pad_frame_cnt);
            return -1;
        }

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

            /* 本文件内的实际读起点：再往前退 Kmax_OFFSET，退成负数就钳到 0
             * （首周期在 bin_Kmax_start_frame 里已钳过，这里只兜后面的边界） */
            cur.file_kmax_frame = next_frame - Kmax_OFFSET;
            if (cur.file_kmax_frame < 0)
                cur.file_kmax_frame = 0;

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



/**
 * @brief 统计所有文件合计能放下多少个完整周期
 * @param path 数据文件路径模板（如 FILE_PATH，含 %05d）
 * @param ffftime  首周期锚点，只用 first_frame_idx（理想起点）
 * @return 成功：完整周期数（可能为 0）；失败：-1
 * @note 以理想起点为基准：B = 总帧数 - first_frame_idx
 *       B 每 30000 帧一个周期，末周期还要多 26 帧（kmax 头部补偿），
 *       故周期数 = (B - 26) / 30000
 */
int64_t bin_allfile_total_cycle(const char *path,const FFFTimeAnchor &ffftime)
{

    int64_t total_frames = 0,val_f = 0;
    
    for(int i = FILE_START; i <= FILE_END; ++i)
    {
        char p[256];
        snprintf(p,sizeof(p),path,i);
        val_f  = bin_frame_count(p);
        if(val_f < 0)
            return -1;
        total_frames += val_f;
    }
    /* 26 = 窗口比步进多出的 51 帧 - 头部已复用的 25 帧
    *    WORK_PERIOD_FRM_CNT - WORK_FRAME_CNT = 51
    *    Kmax_OFFSET = 25 */
    const int64_t tail_need = WORK_PERIOD_FRM_CNT - WORK_FRAME_CNT - Kmax_OFFSET;
    const int64_t available = total_frames - ffftime.first_frame_idx;
    total_frames -= ffftime.first_frame_idx;
    if (available < tail_need)
        return 0;
    return (available - tail_need) / WORK_FRAME_CNT;

}

/**
 * @brief 读取周期锚点列表中所有周期数据，组装为通道优先排布的float数组，连续存入输出缓冲区
 * @param path 数据文件路径模板（如 FILE_PATH，含 %05d）
 * @param cycle_anchor_list 周期锚点表，由 bin_glob_frame 生成
 * @param out 输出float缓冲区，容量必须 >= cycle_anchor_list.size() * ChanNumAll * WORK_PERIOD_ALIGN_CNT
 * @return 0 成功；-1 失败（文件打开失败、fseek定位失败、读帧失败、文件数据耗尽）
 * @note 数据布局 out[i][c][p]，行主序；c范围0~ChanNumAll-1，p范围0~WORK_PERIOD_ALIGN_CNT-1，通道优先，非帧优先
 * @note 磁盘单帧结构：72B帧头 + 256通道float数据；整帧读取，减少频繁fseek破坏IO缓冲
 * @note 单个周期共30051帧，周期起点取自 anchor.file_kmax_frame，一个周期内仅一次文件定位
 * @note 读取过程触达文件末尾时自动切换下一个bin文件，换文件跳过8字节全局文件头，周期内帧计数不重置
 * @note 读取帧数据时跳过72字节帧头，内存排布严格通道优先，和MATLAB对齐
 * @note 
 * !!
 *  磁盘（文件里）：一帧内按通道排
 *      帧 0:  [ch0 的32点][ch1 的32点][ch2 的32点]...[ch255 的32点]
 *      帧 1:  [ch0 的32点][ch1 的32点][ch2 的32点]...[ch255 的32点]
 * 
 *  内存（out）：一个通道排到底
 *      out ─────────────────────────────────────────────────────────
 *      [通道0 的 961632 点]  [通道1 的 961632 点]  ...  [通道127 的 961632 点]
 *      ├─ 帧0的32点
 *      ├─ 帧1的32点
 *      ├─ 帧2的32点
 *      └─ ...
 * !!
 * @code
 * vector<float> buf(cycle_anchor_list.size() * (size_t)ChanNumAll * WORK_PERIOD_ALIGN_CNT);
 * bin_read_cycle(FILE_PATH, cycle_anchor_list, buf.data());
 * @endcode
 */

int bin_read_cycle(const char *path, const vector<CycleAnchor> &cycle_anchor_list, float *out)
{
    if (cycle_anchor_list.empty())
        return 0;

    const size_t cycle_floats = (size_t)USE_CHAN_NUM * WORK_PERIOD_ALIGN_CNT;   /* 128×961632 */

    for (size_t i = 0; i < cycle_anchor_list.size(); ++i)
    {
        const CycleAnchor &anchor = cycle_anchor_list[i];
        float *dst = out + i * cycle_floats;            /* 本周期在 out 里的起点 */

        int  file_id = anchor.file_id;
        char p[256];
        snprintf(p, sizeof(p), path, file_id);

        FILE *fp = nullptr;
        if (!fileop_check(fp, p))
            return -1;
        setvbuf(fp, nullptr, _IOFBF, 1 << 22);          /* 4 MB 缓冲，否则每帧 32 KB 会被拆成 8 次系统调用 */

        /* 一次定位到本周期起点：跳过 8 字节全局头 + 前面 file_kmax_frame 个整帧 */
        if (0 > fseeko(fp, FILE_HDR_BYTES + (off_t)anchor.file_kmax_frame * FRAME_BYTES, SEEK_SET))
        {
            fprintf(stderr, "[ERROR] fseeko failed, path: %s, errno: %d, msg: %s\n",
                    p, errno, strerror(errno));
            fclose(fp);
            return -1;
        }

        uint8_t frame[FRAME_BYTES];                     /* 一帧缓冲：72 B 头 + 32768 B 数据 */
        int64_t f = 0;                                  /* 本周期已读帧数，跨文件时不重置 */
        /*读完一周期所有帧，尾部不足一帧视为垃圾，直接抛弃并打开下一文件*/
        while (f < WORK_PERIOD_FRM_CNT)
        {
            size_t got = fread(frame, 1, FRAME_BYTES, fp);

            if (got < FRAME_BYTES)
            {
                if (ferror(fp))
                {
                    fprintf(stderr, "[ERROR] fread failed, path: %s, errno: %d, msg: %s\n",
                            p, errno, strerror(errno));
                    fclose(fp);
                    return -1;
                }

                /* 本文件读完（含尾部不足一帧的脏数据）：换下一个文件接着读，f 不推进 */
                fclose(fp);
                ++file_id;
                if (file_id > FILE_END)
                {
                    fprintf(stderr, "[ERROR] 数据不足一个完整周期：已用尽到 SIM_%05d\n",
                            FILE_END);
                    return -1;
                }

                snprintf(p, sizeof(p), path, file_id);
                if (!fileop_check(fp, p))
                    return -1;
                setvbuf(fp, nullptr, _IOFBF, 1 << 22);

                if (0 > fseeko(fp, FILE_HDR_BYTES, SEEK_SET))   /* 新文件只跳 8 字节全局头 */
                {
                    fprintf(stderr, "[ERROR] fseeko failed, path: %s, errno: %d, msg: %s\n",
                            p, errno, strerror(errno));
                    fclose(fp);
                    return -1;
                }
                continue;                                    /* 这一帧还没读到，不 ++f */
            }

            /* 跳过 72 字节帧头，只搬前 128 个通道；每个通道 32 点 = 128 字节 */
            const uint8_t *src = frame + FRAME_HDR_BYTES;
            /* 按通道读取，只读有效通道 */
            for (int c = 0; c < USE_CHAN_NUM; ++c)
                //dst + c*961632 ← 先跳到第 c 个通道
                //再跳到这个通道里第 f 帧的 32 点
                memcpy(dst + (size_t)c * WORK_PERIOD_ALIGN_CNT + f * FRM_NUM,
                       src + (size_t)c * FRM_NUM * sizeof(float),
                       FRM_NUM * sizeof(float));

            ++f;
        }

        fclose(fp);
    }

    return 0;
}


/**************************六、周期写盘*************************/

/**
 * @brief 把一个完整周期按 float32 原样写到文件，供与 MATLAB 参考数据逐位对拍
 * @param path_write 输出文件路径（普通路径，不是含 %05d 的模板）
 * @param in 输入缓冲区，长度必须 >= CYCLE_FLOATS（128 × 961632），通道优先连续排布
 * @return 0 成功；-1 失败
 * @note 不做任何数值换算、不转置：内存里怎么排就怎么写，
 *       字节序即本机字节序（x86 / Jetson 都是小端，与 MATLAB 导出一致）
 * @note 一次 fwrite 而不是按通道循环：in 本来就是通道优先连续的，
 *       整块写与分段写落盘结果完全相同，分段只是多 127 次系统调用
 * @note 469 MB 的写，磁盘满 / 配额超限基本只在 fclose 时才暴露，所以那一步必须查返回值
 * @code
 * bin_write_cycle("/mnt/e/.../data/cycle1.raw", buf);
 * @endcode
 */
int bin_write_cycle(const char *path_write, const float *in)
{
    if (path_write == nullptr || in == nullptr)
        return -1;

    FILE *fp = fopen(path_write, "wb");                 /* b：明确二进制 */
    if (fp == nullptr)
    {
        fprintf(stderr, "[ERROR] fopen failed, path: %s, errno: %d, msg: %s\n",
                path_write, errno, strerror(errno));
        return -1;
    }
    setvbuf(fp, nullptr, _IOFBF, 1 << 22);              /* 4 MB 缓冲，和读那边对称 */

    /* 内存本身就是「通道 0 的 961632 点 → 通道 1 …」连续排布，一次写完即可 */
    size_t w = fwrite(in, sizeof(float), (size_t)(USE_CHAN_NUM*WORK_PERIOD_ALIGN_CNT), fp);
    if (w != (size_t)USE_CHAN_NUM*WORK_PERIOD_ALIGN_CNT)
    {
        fprintf(stderr, "[ERROR] fwrite short: 应写 %zu 个 float，实写 %zu，ferror=%d\n",
                (size_t)USE_CHAN_NUM*WORK_PERIOD_ALIGN_CNT, w, ferror(fp));
        fclose(fp);
        return -1;
    }

    /* 469 MB 的写，错误基本只在关文件时暴露 */
    if (fclose(fp) != 0)
    {
        fprintf(stderr, "[ERROR] fclose failed（磁盘满？）, path: %s, errno: %d, msg: %s\n",
                path_write, errno, strerror(errno));
        return -1;
    }
    return 0;
}
