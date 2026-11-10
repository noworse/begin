#ifndef BIN_FORMAT_H
#define BIN_FORMAT_H

/******************************************************************************
 * 文件：bin_format.hpp
 * 作用：bin 原始数据的【格式常量】＋【结构体定义】，全工程唯一的格式事实来源。
 *       32840 / 961632 / 30051 / 816 这些数字只允许在这里推导一次，
 *       其它文件一律引用宏，不许再写一遍裸数字。
 * 数据：SIM_%05d.bin（MATLAB 仿真输出），字节布局与 Main_LFM_AR.m、readTime.m 一致。
 * 布局：8 B 文件头 ＋ N ×（72 B 帧头 ＋ 32768 B 数据体）
 ******************************************************************************/

#include <stdint.h>
#include <stddef.h>
#include "function.hpp"
#include <cstdint>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include <sys/types.h>
#include <string>
#include <iostream>
#include <cmath>
#include <vector>

/**************************一、文件 / 帧 / 通道基本尺寸*************************/

#define FILE_HDR_BYTES      8       /* 每个文件开头的文件头 */
#define FRAME_HDR_BYTES     72      /* 每帧的帧头 */
#define CHAN_NUM            256     /* 阵列总通道数 */
#define USE_CHAN_NUM        128     /* 实际只用前 128 个（后 128 为无效通道） */
#define FRM_NUM             32      /* 每帧 32 个采样点 */
#define FS                  32000   /*采样频率32000hz*/

/**************************二、主动工作周期长度*************************/

#define WORK_PERIOD         30      /*主动工作周期30s*/

/**************************三、每帧字节数*************************/

//一帧数据32 点 × 256 通道 × float32
//每帧字节= 72 + 256×32×4
//FRAME_BYTES_MEASURED 是实测锚：6 个 bin 的 (大小-8) 都能被它整除
#define FRAME_BYTES_MEASURED 32840L /*实测锚*/
#define FRAME_BYTES          (FRAME_HDR_BYTES + CHAN_NUM*FRM_NUM*4)
#define FRAME_BYTES_NOHEAD   (CHAN_NUM*FRM_NUM*4)
//真实数据 只有128 个有效通道
#define FRAME_BYTES_REAL     (FRAME_HDR_BYTES+USE_CHAN_NUM*FRM_NUM*4)

/**************************四、周期 → 采样点 → 帧*************************/

//一个通道工作周期理想采样点 = 30 s × 32000 Hz
#define WORK_PERIOD_CNT     (WORK_PERIOD*FS)
//一个通道工作周期理想帧数 = 960000 / 32 = 30000，这是相邻周期起点的【步进】
//注意与 WORK_PERIOD_FRM_CNT(30051) 区分：30051 是每周期实际【读取窗口】长度
#define WORK_FRAME_CNT     ((WORK_PERIOD*FS)/FRM_NUM)
//一帧的时长（毫秒）：FRM_NUM/FS = 32/32000 = 1 ms
//“帧号 <-> 时间”按 1:1 换算全靠这个等式，换采样率后必须仍整除，否则下方断言会拦住
#define FRAME_MS            ((1000*FRM_NUM)/FS)

/**************************五、Kmax 波束时延保护带*************************/

/*
Kmax 表示阵列两端之间（阵元 1 到阵元 128）同一波前
到达的时间差，换算成采样点数后再加 3 点余量
 Kmax = 最大时延补偿点数
 阵列两端距离差 = 最后一个阵元X坐标 - 第一个 = 39.0m左右
 传播时间 = 距离差/声速，乘以采样率就是采样点数，再加3点余量
 Kmax = round((ArrayXCord(128)-ArrayXCord(1))/C*fs) + 3;
*/
/* Kmax：38.10 m ÷ 1500 m/s × 32000 Hz = 812.8 → 813，+3 余量 */
#define KMAX_DELAY_CNT       813
#define KMAX_MARGIN          3
#define Kmax                 (KMAX_DELAY_CNT + KMAX_MARGIN)   /* 816 点 = 25.5 ms */
//一帧能装多少点 = 32，所以回退量向下取整 25 帧；配合“向上取整到帧”正好等价于回退 25.5 ms
#define Kmax_OFFSET          (Kmax/FRM_NUM)
//一个通道工作周期实际长度 = 理想 960000 点 + 前后各 Kmax 点 = 961632 点
#define WORK_PERIOD_REAL_CNT (WORK_PERIOD_CNT+2*Kmax)
//32采样点对齐（帧对齐）实际长度,断言用
//961632 已能被 32 整除，此宏当前是恒等变换；保留它是为了将来改 Kmax 后仍能自动补到帧边界
#define WORK_PERIOD_ALIGN_CNT (((WORK_PERIOD_REAL_CNT+FRM_NUM-1)/FRM_NUM)*FRM_NUM)
//一个通道工作周期的帧数（= 961632/32 = 30051），读 bin 时的帧循环上限
#define WORK_PERIOD_FRM_CNT  (WORK_PERIOD_ALIGN_CNT / FRM_NUM)

/* 一个周期在内存里的 float 个数 = 有效通道数 × 帧对齐点数 = 128 × 961632 = 123,088,896
 * 走 size_t：单周期字节数 4.92 亿还在 int32 内，但多周期求和会溢出成负数，
 * 那种 bug 表现为分配失败或越界写，回头很难追 */
#define CYCLE_FLOATS  ((size_t)USE_CHAN_NUM * WORK_PERIOD_ALIGN_CNT)
/* 一个周期的字节数 = 492,355,584（469.5 MiB）；落盘文件必须恰好是这个大小 */
#define CYCLE_BYTES   (CYCLE_FLOATS * sizeof(float))

/**************************六、72 字节帧头内部偏移*************************/

/* 帧头 72 B 的字段位置（与 readTime.m 的 fseek 序列一一对应）：
 *   偏移 0  .. 11   保留
 *   偏移 12 .. 15   date  uint32，BCD，如 0x20240929
 *   偏移 16 .. 19   time  uint32，BCD，如 0x14412900 → 字节序 百分秒·秒·分·时
 *   偏移 20 .. 23   ns    uint32，纳秒，0 ~ 999999999
 *   偏移 24 .. 71   保留
 * readTime.m 的读法：fseek 12 → 读 date、time、ns 共 12 B → fseek 48 + 32768 */
#define FRAME_HDR_BYTES_FNON     12     /*帧头保留段1：date 之前的保留字节（readTime.m 用 fseek 12 跳过）*/
#define FRAME_HDR_BYTES_ENON     (16*3) /*帧头后保留字节*/

/**************************七、运行配置（路径 / 文件范围）*************************/

//格式定义
#define FILE_PATH "/home/waster/data/sim/SIM_%05d.bin"  /* 数据文件路径模板，%05d = 文件编号 */
#define OUTP_PATH "/home/waster/data/ret/RER_%05d.bin"  /* 数据文件路径模板，%05d = 文件编号 */
#define DATATIME_EXAMPLE "2024-09-29 10:00:00.000"      /* 时间串格式示例，用于核对 bin_read_time 输出 */
#define FILE_START 1                                     /* 起始文件编号，1-based，对应 MATLAB 的 fStart */
#define FILE_END   6                                     /* 结束文件编号 */

/**************************八、周期定位结构体*************************/

//推算首周期
//首周期锚点：bin_find_start_frame 填理想值，bin_Kmax_start_frame 修正出实际读起点
struct FFFTimeAnchor//CPP风格不使用typdef
{
    int64_t first_frame_idx;    // 理想起点帧号（文件内 0-based，未扣 Kmax），只用于对账与打印
    int64_t first_Kmax_idx;     // 实际读起点帧号（文件内 0-based，已扣 Kmax、已钳位）
    uint32_t first_sec;         // 周期名义起点（当日累计秒）

};

/* 一周期在全局帧轴上的定位结果，由 bin_glob_frame 填
 *
 *   两个帧号在【首周期被钳位时】会分叉：
 *     global_frame      理想起点 = 30 s 刻度所在的帧，未扣 Kmax
 *     global_kmax_frame 实际读起点 = global_frame - 25（被钳位时为 0）
 *   读数据只能用 global_kmax_frame；global_frame 只用于对账、打印、生成时标。 */
struct CycleAnchor
{
    int     cycle_id;         // 周期号，1-based
    int64_t global_frame;     // 起点理想全局帧号，0-based（未扣 Kmax）
    int64_t global_kmax_frame;// 起点全局帧号，0-based，已扣 Kmax ★读数据的唯一真值
    int     file_id;          // 起点所在文件，1-based（= 文件名里的编号）；只表示起点，读取可能跨文件
    int64_t file_frame;       // 起点在文件内的帧号，0-based（派生量，理想值，未扣 Kmax）
    int64_t file_kmax_frame;  // 起点在文件内的【实际读起点】帧号，0-based，已扣 Kmax、已钳位
                              // ★ 读数据只用这一个，不要再自己 -Kmax_OFFSET
    bool    enable_frame_pad; //本文件剩余帧不足一个步进时为 true，表示下一周期落到下一文件
    int64_t pad_frame_cnt;    ///< 下一周期在【下一文件】内的起始帧号；仅 enable_frame_pad 为 true 时有效
};


/**************************九、断言校验*************************/

/* 每一条断言都对应一个“改了就错”的前提，编译期拦住比运行时对拍便宜得多 */
static_assert(sizeof(float) == 4, "float 必须是 4 字节");
static_assert(FRAME_BYTES == FRAME_BYTES_MEASURED,   "推导值与实测锚 32840 不符");
static_assert(WORK_PERIOD_CNT == 960000,             "一周期采样点应为 960000");
static_assert((1000*FRM_NUM) % FS == 0,              "一帧不是整数毫秒，帧号不能当毫秒用");
static_assert(FRAME_MS == 1,                         "一帧应为 1 ms（32 点 / 32000 Hz）");
static_assert(Kmax == 816,                           "Kmax 应与 Main_LFM_AR.m L68 的 816 一致");
static_assert(WORK_PERIOD_REAL_CNT == 961632,        "一周期实际长度应为 961632");
static_assert(WORK_PERIOD_ALIGN_CNT == 961632,       "帧对齐后应为 961632（961632 能被 32 整除）");
static_assert(CYCLE_FLOATS == 123088896UL,            "一周期 float 数应为 123088896");
static_assert(CYCLE_BYTES  == 492355584UL,            "一周期字节数应为 492355584");

/**************************十、帧头接收结构体*************************/

/* 72 字节帧头：先按裸字节收，具体字段靠偏移自行解析（见第六节）*/
typedef struct 
{
    uint8_t raw[FRAME_HDR_BYTES];
}frame_header_t;

static_assert(sizeof(frame_header_t) == FRAME_HDR_BYTES, "帧头不能有 padding");

#endif // !BIN_FORMAT_H
