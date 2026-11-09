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
    ffftime.first_Kmax_idx = ffftime.first_frame_idx - Kmax_OFFSET;
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
    const int64_t kmax_offset = Kmax / FRM_NUM;   // 816/32 = 25，等价于 ceil(-25.5)
    ffftime.first_Kmax_idx -= kmax_offset;

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

int bin_glob_frame(FFFTimeAnchor ffftime,vector<CycleAnchor>& cycle_anchor_list)
{
    CycleAnchor cycle_anchor;
    int64_t frame_count = 0,count=0;
    int64_t fcount_diff,real_frame;
    char path_buf[256];
    cycle_anchor.file_id = FILE_START;
    cycle_anchor.cycle_id = 1;
    cycle_anchor.file_frame = ffftime.first_frame_idx;
    cycle_anchor.global_frame = ffftime.first_frame_idx;
    cycle_anchor.global_kmax_frame = ffftime.first_Kmax_idx;
    cycle_anchor.enable_frame_pad = false;
    cycle_anchor_list.emplace_back(cycle_anchor);
    for(int file_id =FILE_START;file_id<=FILE_END;file_id++)
    {
        if(!Check(snprintf(path_buf,sizeof(path_buf),FILE_PATH,file_id)))
            return -1;
        if(!Check(frame_count = bin_frame_count(path_buf)))
            return -1;
        if(file_id == FILE_START)
        {
            fcount_diff = ((frame_count-ffftime.first_frame_idx+WORK_FRAME_CNT - 1)/WORK_FRAME_CNT);
            real_frame = frame_count-ffftime.first_frame_idx;
        }
        else{
            fcount_diff = ((frame_count+WORK_FRAME_CNT - 1)/WORK_FRAME_CNT);
            real_frame  = frame_count;
        }
        
        if(cycle_anchor_list.back().enable_frame_pad)
        {
            real_frame -= cycle_anchor_list.back().pad_frame_cnt;

        }
        for(int64_t fcount = 0;fcount<fcount_diff;fcount++)
        {
            cycle_anchor.file_id = file_id;
            cycle_anchor.cycle_id = cycle_anchor_list[count].cycle_id+1;
            cycle_anchor.global_frame = cycle_anchor_list[count].global_frame+WORK_FRAME_CNT;
            cycle_anchor.global_kmax_frame = cycle_anchor.global_frame - Kmax_OFFSET;
            if(cycle_anchor_list.back().enable_frame_pad)
            {
                cycle_anchor.file_frame = cycle_anchor_list.back().pad_frame_cnt;
            }
            else
            {
                cycle_anchor.file_frame = cycle_anchor_list[count].file_frame+WORK_FRAME_CNT;
            }
            
            if(frame_count - cycle_anchor_list[count].file_frame <= WORK_FRAME_CNT)
            {
                cycle_anchor.enable_frame_pad = true;
                cycle_anchor.pad_frame_cnt =WORK_FRAME_CNT - (real_frame - cycle_anchor_list[fcount].file_frame);
            }
            else {
                cycle_anchor.enable_frame_pad = false;
            }
            cycle_anchor_list.emplace_back(cycle_anchor);
            count++;
        }   
    }
    return count;
}