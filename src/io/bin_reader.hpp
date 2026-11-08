#ifndef BIN_READER_H
#define BIN_READER_H

#include "bin_format.hpp"
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
/* 返回帧数；失败返回 -1 */
int64_t bin_frame_count(const char *path);
/* 读第 idx 帧（0 基）的 72 字节帧头到 out；成功 0，失败 -1 */
int bin_read_frame_header(const char *path, int64_t idx, uint8_t *out72);
/* 读第 idx 帧的数据体：out 为 256×32 的 float32（行=通道，列=采样点）
 * 成功 0，失败 -1 */
int bin_read_frame_data(const char *path, int64_t idx, float *out_cs);

#endif // !BIN_READER_H
