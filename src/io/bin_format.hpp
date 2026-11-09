#ifndef BIN_FORMAT_H
#define BIN_FORMAT_H

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
/**************************固定数据宏定义*************************/
#define FILE_HDR_BYTES      8       /* 每个文件开头的文件头 */
#define FRAME_HDR_BYTES     72      /* 每帧的帧头 */
#define CHAN_NUM            256     /* 阵列总通道数 */
#define USE_CHAN_NUM        128     /* 实际只用前 128 个 */
#define FRM_NUM             32      /* 每帧 32 个采样点 */
#define FS                  32000   /*采样频率32000hz*/
#define WORK_PERIOD         30      /*主动工作周期30s*/
//一帧数据32 点 × 256 通道 × float32
//每帧字节= 72 + 256×32×4
#define FRAME_BYTES_MEASURED 32840L /*实测锚*/
#define FRAME_BYTES          (FRAME_HDR_BYTES + CHAN_NUM*FRM_NUM*4)
#define FRAME_BYTES_NOHEAD   (CHAN_NUM*FRM_NUM*4)
//一工作周期理想采样点
#define WORK_PERIOD_CNT     (WORK_PERIOD*FS)
//一工作周期理想帧数
#define WORK_FRAME_CNT     ((WORK_PERIOD*FS)/FRM_NUM)
//一帧的时长（毫秒）：FRM_NUM/FS = 32/32000 = 1 ms
//“帧号 <-> 时间”按 1:1 换算，换采样率后必须仍整除，否则下方断言会拦住
#define FRAME_MS            ((1000*FRM_NUM)/FS)
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
#define Kmax                 (KMAX_DELAY_CNT + KMAX_MARGIN)
#define Kmax_OFFSET          (Kmax/FRM_NUM)
//一工作周期实际长度
#define WORK_PERIOD_REAL_CNT (WORK_PERIOD_CNT+2*Kmax)
//32采样点对齐（帧对齐）实际长度,断言用
#define WORK_PERIOD_ALIGN_CNT (((WORK_PERIOD_REAL_CNT+FRM_NUM-1)/FRM_NUM)*FRM_NUM)
//一工作周期的帧数（= 961632/32 = 30051），读 bin 时的帧循环上限
#define WORK_PERIOD_FRM_CNT  (WORK_PERIOD_ALIGN_CNT / FRM_NUM)



#define FRAME_HDR_BYTES_FNON     12     /*帧头保留段1：date 之前的保留字节（readTime.m 用 fseek 12 跳过）*/
#define FRAME_HDR_BYTES_ENON     (16*3) /*帧头后保留字节*/


//格式定义
#define FILE_PATH "/home/waster/data/sim/SIM_%05d.bin"
#define DATATIME_EXAMPLE "2024-09-29 10:00:00.000"
#define FILE_START 1
#define FILE_END   1

//推算首周期
struct FFFTimeAnchor//CPP风格不使用typdef
{
    int64_t first_frame_idx;    // 理想起点帧号（文件内 0-based，未扣 Kmax）
    int64_t first_Kmax_idx;    // Kmax起点帧号
    uint32_t first_sec;         // 周期名义起点（当日累计秒）

};

/* 一周期在全局帧轴上的定位结果 */
struct CycleAnchor
{
    int     cycle_id;         // 周期号，1-based
    int64_t global_frame;     // 起点理想全局帧号，0-based
    int64_t global_kmax_frame;// 起点全局帧号，0-based，已扣 Kmax ★唯一真值
    int     file_id;          // 起点所在文件，1-based（= 文件名里的编号）
    int64_t file_frame;       // 起点在文件内的帧号，0-based（派生量）
    bool    enable_frame_pad; //是否需要补帧
    int64_t pad_frame_cnt;    ///< 补帧数量，==仅 enable_frame_pad 为true时有效==
};


/**************************断言校验*************************/

static_assert(sizeof(float) == 4, "float 必须是 4 字节");
static_assert(FRAME_BYTES == FRAME_BYTES_MEASURED,   "推导值与实测锚 32840 不符");
static_assert(WORK_PERIOD_CNT == 960000,             "一周期采样点应为 960000");
static_assert((1000*FRM_NUM) % FS == 0,              "一帧不是整数毫秒，帧号不能当毫秒用");
static_assert(FRAME_MS == 1,                         "一帧应为 1 ms（32 点 / 32000 Hz）");
static_assert(Kmax == 816,                           "Kmax 应与 Main_LFM_AR.m L68 的 816 一致");
static_assert(WORK_PERIOD_REAL_CNT == 961632,        "一周期实际长度应为 961632");
static_assert(WORK_PERIOD_ALIGN_CNT == 961632,       "帧对齐后应为 961632（961632 能被 32 整除）");

/**************************数据接收结构体定义*************************/
/* 72 字节帧头：先按裸字节收*/
typedef struct 
{
    uint8_t raw[FRAME_HDR_BYTES];
}frame_header_t;

static_assert(sizeof(frame_header_t) == FRAME_HDR_BYTES, "帧头不能有 padding");

#endif // !BIN_FORMAT_H