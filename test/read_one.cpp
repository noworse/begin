#include "bin_reader.hpp"

using namespace std;


int main(int argc,char **argv)
{
    
    if(argc<2)
    {
        fprintf(stderr,"[WARN]:请指定测试命令:\n \
            readframe:读取帧数\n \
            frame_cycle:查找起始帧全局序号\n \
            cycles:打印全部周期起点与时刻\n \
            frame_header:读取帧头 需要指明第几帧 \
            例如：“frame_header 1” \n \
            frame_data:读数据体 需要指明第几帧\n \
            frame_date:读日期时间 需要指明第几帧\n \
            ");

            return -1;
    }
    int64_t ret;
    int32_t file_start = FILE_START;
    char path_buf[256];
    snprintf(path_buf,sizeof(path_buf),FILE_PATH,file_start);
    string command_in(argv[1]);
    if(command_in == "readframe") 
    {
        ret=bin_frame_count(path_buf);
        if(ret!=-1)
        {
            cout<<"共读到"<<ret<<"帧数据"<<endl;
        }
        else
            return -1;
    }
    else if(command_in == "frame_cycle")
    {
        FFFTimeAnchor ffftime;
        if( bin_find_start_frame(path_buf,WORK_PERIOD,ffftime) ==0)
        {
            cout<<"读取首帧时间，索引成功"<<endl;
            cout<<"首帧索引："<<ffftime.first_frame_idx<<endl;
            cout <<"首帧时间："<<ffftime.first_sec<<endl;
        }
        else
        {
            cout<<"读取首帧时间，索引失败"<<endl;
            return -1;
        }
    }
    else if(command_in == "cycles")
    {
        // 1) 首周期理想起点（未扣 Kmax）
        FFFTimeAnchor ffftime{};
        if(bin_find_start_frame(path_buf,WORK_PERIOD,ffftime) != 0)
        {
            cout<<"首周期定位失败"<<endl;
            return -1;
        }
        // 2) 扣掉 Kmax 保护带，得到实际读起点
        int kmax_ret = bin_Kmax_start_frame(ffftime);
        if(kmax_ret < 0)
        {
            cout<<"Kmax 回退失败"<<endl;
            return -1;
        }
        if(kmax_ret > 0)
            cout<<"[提示] 首文件开头保护带不足，已钳到第 0 帧"<<endl;

        // 3) 推算全部周期
        vector<CycleAnchor> list;
        int ncycle = bin_glob_frame(ffftime,list);
        if(ncycle < 0)
        {
            cout<<"全局索引推算失败"<<endl;
            return -1;
        }

        // 4) 独立统计总帧数，用于判定每个周期是否读得满
        int64_t total_frames = 0;
        for(int f = FILE_START; f <= FILE_END; ++f)
        {
            char p[256];
            snprintf(p,sizeof(p),FILE_PATH,f);
            int64_t n = bin_frame_count(p);
            if(n < 0)
                return -1;
            total_frames += n;
        }
        const int64_t period_frames = WORK_PERIOD_ALIGN_CNT / FRM_NUM;   // 30051

        printf("%-4s %-10s %-11s %-11s %-11s %-24s %s\n",
               "cyc","file","file_frame","global","kmax","start_time","fit");
        for(size_t i = 0; i < list.size(); ++i)
        {
            const CycleAnchor &c = list[i];
            char p[256];
            snprintf(p,sizeof(p),FILE_PATH,c.file_id);

            // 实际读起点在本文件内的帧号（首周期被钳位时为 0）
            int64_t kmax_local = c.file_frame - Kmax_OFFSET;
            if(kmax_local < 0)
                kmax_local = 0;

            string ts;
            if(bin_read_time(p,kmax_local,ts) != 0)
                ts = "<读取失败>";

            bool fit = (c.global_kmax_frame + period_frames <= total_frames);
            printf("%-4d SIM_%05d  %-11lld %-11lld %-11lld %-24s %s\n",
                   c.cycle_id, c.file_id,
                   (long long)c.file_frame,
                   (long long)c.global_frame,
                   (long long)c.global_kmax_frame,
                   ts.c_str(),
                   fit ? "ok" : "越界");
        }
        cout<<"共 "<<list.size()<<" 个周期，总帧数 "<<total_frames
            <<"，每周期需 "<<period_frames<<" 帧"<<endl;
    }
    else if(argc != 3)
    {
        fprintf(stderr,"[WARN]:参数错误:\n \
            frame_header:读取帧头 需要指明第几帧 \
            例如：“frame_header 1” \n \
            frame_data:读数据体 需要指明第几帧\n");
        return -1;
    }
    /*
    long long strtoll(const char *nptr, char **endptr, int base);
    自动跳过开头空白 读到不是数字的字符就停止转换
    溢出：超出 long long 范围,返回 LLONG_MAX /
    LLONG_MIN，同时设置全局错误标志 errno
    完全无法转换（字符串是 "abc"）：返回 0
    */
    else if (command_in == "frame_header")
    {
        int64_t idx = strtoll(argv[2], nullptr, 10);
        uint8_t *data =(uint8_t*)malloc(72*sizeof(uint8_t));
        ret=bin_read_frame_header(path_buf,idx,data);
        if(ret!=-1)
        {
            free(data);
            cout<<"读取帧头成功"<<endl;
        }
        else
        {
            free(data);
            cout<<"读取帧头失败"<<endl;
            return -1;
        }
    }
    else if (command_in == "frame_data")
    {
        int64_t idx = strtoll(argv[2], nullptr, 10);
        float *data =(float*)malloc(CHAN_NUM*FRM_NUM*sizeof(float));
        ret=bin_read_frame_data(path_buf,idx,data);
        if(ret!=-1)
        {
            free(data);
            cout<<"读取帧数据成功"<<endl;
        }
        else
        {
            free(data);
            cout<<"读取帧数据失败"<<endl;
            return -1;
        }
    
    }
    else if (command_in == "frame_date")
    {
        int64_t idx = strtoll(argv[2], nullptr, 10);
        string buf;
        if(bin_read_time(path_buf,idx,buf) != -1)
        {
            cout<<"读取帧日期时间成功"<<endl;
            cout<<buf<<endl;
        }
        else
        {
            cout<<"读取帧日期时间失败"<<endl;
            return -1;
        }
    }
    
    else
        cout<<"未知命令"<<endl;
    return 0;
}