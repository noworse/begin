#ifndef BIN_READER_H
#define BIN_READER_H

#include "bin_format.hpp"


using namespace std;

#define Check(err) err_check(err,__FILE__,__LINE__)

bool err_check(int err,const char* filename, int lineNumber);

/* 返回帧数；失败返回 -1 */
int64_t bin_frame_count(const char *path);
/* 读第 idx 帧（0 基）的 72 字节帧头到 out；成功 0，失败 -1 */
int bin_read_frame_header(const char *path, int64_t idx, uint8_t *out72);
/* 读第 idx 帧的数据体：out 为 256×32 的 float32（行=通道，列=采样点）
 * 成功 0，失败 -1 */
int bin_read_frame_data(const char *path, int64_t idx, float *out_cs);

/* 读第 idx 帧帧头里的日期/时间/纳秒，格式化成
 * "2024-09-29 10:00:00.000" 写入 buf（>=32 字节）；成功 0，失败 -1 */
int bin_read_time(const char *path, int64_t idx, string &buf);

/* 读首文件第 0 帧时间，算【未扣 Kmax】的理想周期起点：
 *   first_frame_idx = 理想起点帧号（文件内 0-based）
 *   first_sec       = 周期名义起点（当日累计秒）
 * 严格大于语义：秒正好落在刻度上时仍跳下一个周期（同 Main_LFM_AR.m）。
 * 越界检查只针对理想值；Kmax 回退与最终取整由 Kmax 处理函数负责。
 * 成功 0，失败 -1 */
int bin_find_start_frame(const char *path,  int cycle_sec,FFFTimeAnchor& ffftime);

//首帧中加入最大时延补偿点数
int bin_Kmax_start_frame(FFFTimeAnchor& ffftime);

#endif // !BIN_READER_H
