#ifndef FUNCTION_H
#define FUNCTION_H

#include <stdio.h>
#include <stdint.h>

enum datetime
{
    Date,
    Time_tatol,
    Time_intuitive
    
};




//将打包为BCD格式的32bit日期原始值，转为十进制整数
uint64_t bcd_date_to_integer(uint32_t raw_date,datetime dt);

#endif // !FUNCTION_H