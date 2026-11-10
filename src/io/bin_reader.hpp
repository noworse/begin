#ifndef BIN_READER_H
#define BIN_READER_H

/******************************************************************************
 * 文件：bin_reader.hpp
 * 作用：bin 文件读取与周期定位的对外接口。
 * 分层：
 *   ① 公共工具       err_check / Check   统一错误检查
 *   ② 基础读取       bin_frame_count / bin_read_frame_header /
 *                    bin_read_frame_data / bin_read_time
 *   ③ 周期定位       bin_find_start_frame（理想起点）
 *                    bin_Kmax_start_frame（扣保护带）
 *                    bin_glob_frame（推算全部周期）
 * 约定：所有帧号 idx 一律【0-based，文件内】；失败统一返回 -1 或 0 以外的值。
 ******************************************************************************/

#include "bin_format.hpp"
#include <vector>

using namespace std;

/**************************一、公共工具*************************/

//把“出错才返回负数”的调用包起来，出错时打印文件名和行号
//用法：if(!Check(某返回值)) return -1;    成功→true，出错→false
#define Check(err) err_check(err,__FILE__,__LINE__)

bool err_check(int err,const char* filename, int lineNumber);

/**************************二、基础读取*************************/

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

/**************************三、周期定位*************************/

/* 读首文件第 0 帧时间，算【未扣 Kmax】的理想周期起点：
 *   first_frame_idx = 理想起点帧号（文件内 0-based）
 *   first_sec       = 周期名义起点（当日累计秒）
 * 严格大于语义：秒正好落在刻度上时仍跳下一个周期（同 Main_LFM_AR.m）。
 * 越界检查只针对理想值；Kmax 回退与最终取整由 Kmax 处理函数负责。
 * 成功 0，失败 -1 */
int bin_find_start_frame(const char *path,  int cycle_sec,FFFTimeAnchor& ffftime);

//首帧中加入最大时延补偿点数
//把理想起点往前退 Kmax_OFFSET(25) 帧得到实际读起点；退成负数则钳到 0（同 MATLAB find 落第 1 帧）
//返回：0 正常回退；1 保护带不足已钳位；-1 失败
int bin_Kmax_start_frame(FFFTimeAnchor& ffftime);

/* 由首周期锚点推算出全部周期起点，依次追加到 cycle_anchor_list。
 * 返回推算出的周期个数；失败 -1。
 * 注意：本函数不校验“起点 + 30051 是否超出数据总量”，由调用方判定。 */
int bin_glob_frame(FFFTimeAnchor ffftime, vector<CycleAnchor> &cycle_anchor_list);

//统计所有文件合计能放下多少个完整周期；成功返回周期数（可能为 0），失败返回 -1
//以理想起点为基准：B = 总帧数 - first_frame_idx，周期数 = (B - 26) / 30000
int64_t bin_allfile_total_cycle(const char *path, const FFFTimeAnchor &ffftime);


/* 逐周期读成 float 数组：第 i 个周期占 USE_CHAN_NUM*WORK_PERIOD_ALIGN_CNT 个 float
 * out 长度必须 >= list.size() * 128 * 961632
 * 布局 out[i][c][p]，行主序，c = 0..127，p = 0..961631（通道优先）
 * 成功 0，失败 -1 */
int bin_read_cycle(const char *path, const vector<CycleAnchor> &cycle_anchor_list, float *out);

#endif // !BIN_READER_H
