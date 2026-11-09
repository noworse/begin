#include "bin_reader.hpp"

using namespace std;


int main(int argc,char **argv)
{
    
    if(argc<2)
    {
        fprintf(stderr,"[WARN]:请指定测试命令:\n \
            readframe:读取帧数\n \
            frame_cycle:查找起始帧全局序号\n \
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