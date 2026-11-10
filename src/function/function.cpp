/******************************************************************************
 * 文件：function.cpp
 * 作用：BCD 打包时间戳的解码实现（日期 / 当日总秒 / HHMMSS 三种输出）。
 * 关键：BCD 是“一个字节装两位十进制”，拆位必须按 (高4位*10 + 低4位)，
 *       不能用整数运算直接取十进制位。
 ******************************************************************************/

#include "function.hpp"


/**************************一、BCD 解码*************************/

/**
 * @brief  BCD打包32位原始时间戳统一解码函数，支持解析日期 / 时分秒
 * @param  raw_date  uint32_t 原始32bit BCD数据包，字节序：[Y1/Y2/H, M, D/S]（小端）
 * @param  dt        枚举标记，指定当前raw是日期还是时分秒，决定解码分支
 * @return uint64_t  解码结果：
 *                   - dt == data：返回 YYYYMMDD 十进制整数，例 20261107
 *                   - dt == time_tatol：返回当日总秒数 hour*3600 + min*60 + sec
 *                   - dt == time_intuitive：返回 HHMMSS 十进制整数，例 144103
 * @note   1. 单字节BCD规则：高4bit十位，低4bit个位，与MATLAB dec2hex解析逻辑对齐
 *         2. 输入raw统一为uint32_t，时分秒复用相同的字节提取位置：
 *            D=百分秒、M=秒、Y2=分、Y1=时（比日期模式整体高一个字节）
 *         3. 注意：枚举 data / time_tatol / time_intuitive 需要提前定义
 */
uint64_t bcd_date_to_integer(uint32_t raw_date,datetime dt)
{
    // 从32bit原始数提取4个独立字节
    // 日期模式：D=日，M=月，Y2=年份后两位，Y1=年份前两位
    // 时分秒模式：D=秒，M=分，Y2=小时，Y1未使用
    uint8_t D  = raw_date & 0x000000FF;         // 字节0
    uint8_t M  = (raw_date >> 8)  & 0x000000FF; // 字节1
    uint8_t Y2 = (raw_date >> 16) & 0x000000FF; // 字节2
    uint8_t Y1 = (raw_date >> 24) & 0x000000FF; // 字节3

    /**
     * @brief 单字节BCD码转为普通十进制整数
     * @param  bcd  输入1字节BCD，例如0x26 代表数字26
     * @retval 解码后的十进制值
     */
    auto bcd2val = [](uint8_t bcd) -> uint8_t {
        return ((bcd >> 4) * 10) + (bcd & 0x0F);
    };
    
    // 分支1：解码日期，输出 YYYYMMDD
    if(dt == Date)
    {
        uint16_t year  = bcd2val(Y1) * 100 + bcd2val(Y2);
        uint8_t  month = bcd2val(M);
        uint8_t  day   = bcd2val(D);
        return (uint64_t)year * 10000 + (uint64_t)month * 100 + day;
    }
    
    // 分支2：解码时分秒，两种输出格式
    else if((dt == Time_tatol) || (dt == Time_intuitive))
    {
        /* raw 0x14412900 的小端内存字节是 00 29 41 14：
           D=百分秒(忽略) · M=秒 · Y2=分 · Y1=时 */
        uint8_t sec  = bcd2val(M);
        uint8_t min  = bcd2val(Y2);
        uint8_t hour = bcd2val(Y1);

        // 输出：从当日0点开始累计总秒数
        if(dt == Time_tatol)
            return ((uint64_t)hour * 3600U + (uint64_t)min * 60U + sec);
        // 输出：HHMMSS 直观数字，如14时03分05秒 → 140305
        else if(dt == Time_intuitive)
            return ((uint64_t)hour * 10000 + (uint64_t)min * 100 + sec);
    }
    return 0;
}


