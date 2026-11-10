/******************************************************************************
 * 文件：read_one.cpp
 * 作用：bin 读取与周期定位的手工测试入口（功能验证用，不做健壮性设计）。
 * 命令：
 *   readframe [file]           读文件总帧数（file 省略时用 FILE_START）
 *   frame_cycle [file]         只定位首周期起点
 *   cycles                     打印全部周期的起点帧号 / 文件归属 / 起始时刻
 *   cycle_read <k>             读第 k 个周期（1-based）并抽样自校验
 *   frame_header <idx> [file]  读第 idx 帧的 72 字节帧头
 *   frame_data   <idx> [file]  读第 idx 帧的数据体（256×32 float）
 *   frame_date   <idx> [file]  读第 idx 帧的时间戳
 * 说明：
 *   ① 帧号 idx 一律 0-based（文件内）；
 *   ② 文件号 file 为 1-based，范围 FILE_START ~ FILE_END，省略时取 FILE_START；
 *   ③ 每个带参命令自己检查参数个数，不再靠链中间的 argc 守卫。
 * 校验职责（避免重复校验，2026-11-10 定）：
 *   位置正确性 —— 起点帧号、跨文件进位、越界，由 bin_glob_frame 内部保证，
 *                本文件不重复检查（否则同一判据写两遍，改一处漏一处）；
 *   数据正确性 —— 帧头有没有跳过、通道顺序、转置，由 cycle_read 的对拍检查；
 *   唯一保留的「重复」是交叉验证：bin_glob_frame 与 bin_allfile_total_cycle
 *   的周期数互比，两条推导路径不同，属验证手段，不算冗余。
 ******************************************************************************/

#include "bin_reader.hpp"
#include <chrono>

using namespace std;

/**************************内部工具*************************/

/**
 * @brief 按文件号生成完整路径；越界时回落到 FILE_START
 * @param buf     输出缓冲
 * @param n       缓冲长度
 * @param file_id 文件号，1-based
 * @note 路径模板取 bin_format.hpp 的 FILE_PATH（含 %05d）
 */
static void make_path(char *buf, size_t n, int file_id)
{
    if (file_id < FILE_START || file_id > FILE_END)
        file_id = FILE_START;
    snprintf(buf, n, FILE_PATH, file_id);
}

/** @brief 打印命令清单 */
static void usage(void)
{
    fprintf(stderr,
            "[WARN] 请指定测试命令：\n"
            "  readframe [file]           读取文件总帧数\n"
            "  frame_cycle [file]         定位首周期起点\n"
            "  cycles                     打印全部周期\n"
            "  cycle_read <k>             读第 k 个周期（1-based）并抽样自校验\n"
            "  frame_header <idx> [file]  读取帧头（idx 为 0-based 帧号）\n"
            "  frame_data   <idx> [file]  读取帧数据体\n"
            "  frame_date   <idx> [file]  读取帧时间戳\n");
}

/**
 * @brief 定位全部周期（首周期理想起点 → 扣 Kmax → 推算）
 * @param ff   输出：首周期锚点
 * @param list 输出：周期表
 * @return true 成功；false 失败
 */
static bool locate_cycles(FFFTimeAnchor &ff, vector<CycleAnchor> &list)
{
    char p[256];
    make_path(p, sizeof(p), FILE_START);

    if (bin_find_start_frame(p, WORK_PERIOD, ff) != 0)
    {
        fprintf(stderr, "[ERROR] 首周期定位失败\n");
        return false;
    }

    const int kmax_ret = bin_Kmax_start_frame(ff);
    if (kmax_ret < 0)
    {
        fprintf(stderr, "[ERROR] Kmax 回退失败\n");
        return false;
    }
    if (kmax_ret > 0)
        fprintf(stderr, "[提示] 首文件开头保护带不足，已钳到第 0 帧\n");

    if (bin_glob_frame(ff, list) <= 0)
    {
        fprintf(stderr, "[ERROR] 全局周期推算失败或没有可用周期\n");
        return false;
    }
    return true;
}

/**************************主流程*************************/

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        usage();
        return -1;
    }

    const string command_in(argv[1]);
    char path_buf[256];
    make_path(path_buf, sizeof(path_buf), FILE_START);

    /**************************命令：读取文件帧数*************************/
    if (command_in == "readframe")
    {
        const int fid = (argc >= 3) ? atoi(argv[2]) : FILE_START;
        make_path(path_buf, sizeof(path_buf), fid);

        const int64_t n = bin_frame_count(path_buf);
        if (n < 0)
            return -1;
        printf("SIM_%05d 共 %lld 帧\n", fid, (long long)n);
    }

    /**************************命令：首周期定位*************************/
    else if (command_in == "frame_cycle")
    {
        const int fid = (argc >= 3) ? atoi(argv[2]) : FILE_START;
        make_path(path_buf, sizeof(path_buf), fid);

        FFFTimeAnchor ff{};
        if (bin_find_start_frame(path_buf, WORK_PERIOD, ff) != 0)
        {
            printf("首周期定位失败\n");
            return -1;
        }
        printf("理想起点帧号 = %lld\n", (long long)ff.first_frame_idx);
        printf("名义起点秒   = %u\n", ff.first_sec);
    }

    /**************************命令：打印全部周期*************************/
    else if (command_in == "cycles")
    {
        FFFTimeAnchor ff{};
        vector<CycleAnchor> list;
        if (!locate_cycles(ff, list))
            return -1;

        // 交叉验证：另一套算法（总帧数一次性除）算出的周期数必须一致。
        // 这不是重复校验——两条推导路径不同，任一算错都会在这里暴露。
        const int64_t n_all = bin_allfile_total_cycle(FILE_PATH, ff);
        if (n_all < 0)
        {
            fprintf(stderr, "[ERROR] 总周期数统计失败\n");
            return -1;
        }
        if (n_all != (int64_t)list.size())
        {
            fprintf(stderr, "[ERROR] 两种算法周期数不一致：总周期=%lld，周期表=%zu\n",
                    (long long)n_all, list.size());
            return -1;
        }

        // 定位已由 bin_glob_frame 保证不越界，这里只负责把结果打出来
        printf("%-4s %-10s %-11s %-11s %-11s %s\n",
               "cyc", "file", "file_frame", "global", "kmax", "start_time");
        for (size_t i = 0; i < list.size(); ++i)
        {
            const CycleAnchor &c = list[i];

            char p[256];
            snprintf(p, sizeof(p), FILE_PATH, c.file_id);

            string ts;
            if (bin_read_time(p, c.file_kmax_frame, ts) != 0)
                ts = "<读取失败>";

            printf("%-4d SIM_%05d  %-11lld %-11lld %-11lld %s\n",
                   c.cycle_id, c.file_id,
                   (long long)c.file_frame,
                   (long long)c.global_frame,
                   (long long)c.global_kmax_frame,
                   ts.c_str());
        }
        printf("共 %zu 个周期（另一套算法同为此数）\n", list.size());
    }

    /**************************命令：读一个周期并自校验*************************/
    // 用法：./build/test_read cycle_read 3        读第 3 个周期（1-based）
    // 只做一件事：验「读出来的数对不对」。位置对不对交给 cycles 表，不在这里重复。
    else if (command_in == "cycle_read")
    {
        // ① 必须有周期号
        if (argc < 3)
        {
            fprintf(stderr, "[WARN] cycle_read 需要周期号（1-based），例如 cycle_read 1\n");
            return -1;
        }
        const int k = atoi(argv[2]);

        // ② 定位全部周期（内部已做越界判断，这里只取用）
        FFFTimeAnchor ff{};
        vector<CycleAnchor> list;
        if (!locate_cycles(ff, list))
            return -1;

        // ③ 越界检查仍在，因为下面要 list[k-1]，这个下标必须自己兜住
        if (k < 1 || k > (int)list.size())
        {
            fprintf(stderr, "[ERROR] 周期号 %d 超出范围 1~%zu\n", k, list.size());
            return -1;
        }
        const CycleAnchor &c = list[k - 1];

        printf("第 %d 周期：起点 SIM_%05d 第 %lld 帧\n",
               c.cycle_id, c.file_id, (long long)c.file_kmax_frame);

        // ④ 分配并读一个周期：128 通道 × 961632 点
        const size_t cycle_floats = (size_t)USE_CHAN_NUM * WORK_PERIOD_ALIGN_CNT;
        vector<float> out(cycle_floats);
        vector<CycleAnchor> one{ c };

        const auto t0 = chrono::steady_clock::now();
        const int  r  = bin_read_cycle(FILE_PATH, one, out.data());
        const double sec = chrono::duration<double>(chrono::steady_clock::now() - t0).count();

        if (r != 0)
        {
            printf("bin_read_cycle 失败\n");
            return -1;
        }
        printf("读完：%.1f MB，%.2f s\n", cycle_floats * sizeof(float) / 1048576.0, sec);

        // ⑤ 数据对拍：用另一条路径读【同一个起点帧】，比前 4 个通道共 128 点。
        //    只要帧头没跳、通道顺序错、转置错、起点偏一帧，这 128 点必然不相等。
        char p[256];
        make_path(p, sizeof(p), c.file_id);

        float tmp[CHAN_NUM * FRM_NUM];
        if (bin_read_frame_data(p, c.file_kmax_frame, tmp) != 0)
        {
            printf("单帧读失败\n");
            return -1;
        }

        int bad = 0;
        for (int ch = 0; ch < 4; ++ch)                 // 只比以前 4 个通道
            for (int j = 0; j < FRM_NUM; ++j)          // 每通道 32 点
                if (out[(size_t)ch * WORK_PERIOD_ALIGN_CNT + j] != tmp[ch * FRM_NUM + j])
                    ++bad;

        printf("对拍：通道 0~3 的第 0 帧，128 点中不一致 %d 点\n", bad);
        if (bad != 0)
            return -1;
    }

    /**************************命令：帧头 / 数据体 / 时间（需帧号）*************************/
    else if (command_in == "frame_header")
    {
        if (argc < 3)
        {
            fprintf(stderr, "[WARN] frame_header 需要帧号，例如 frame_header 0\n");
            return -1;
        }
        const int64_t idx = strtoll(argv[2], nullptr, 10);
        make_path(path_buf, sizeof(path_buf), (argc >= 4) ? atoi(argv[3]) : FILE_START);

        vector<uint8_t> data(FRAME_HDR_BYTES);
        if (bin_read_frame_header(path_buf, idx, data.data()) != 0)
        {
            printf("读取帧头失败\n");
            return -1;
        }
        printf("读取帧头成功，前 16 字节：");
        for (int i = 0; i < 16; ++i)
            printf("%02X ", data[i]);
        printf("\n");
    }
    else if (command_in == "frame_data")
    {
        if (argc < 3)
        {
            fprintf(stderr, "[WARN] frame_data 需要帧号，例如 frame_data 0\n");
            return -1;
        }
        const int64_t idx = strtoll(argv[2], nullptr, 10);
        make_path(path_buf, sizeof(path_buf), (argc >= 4) ? atoi(argv[3]) : FILE_START);

        vector<float> data((size_t)CHAN_NUM * FRM_NUM);
        if (bin_read_frame_data(path_buf, idx, data.data()) != 0)
        {
            printf("读取帧数据失败\n");
            return -1;
        }
        printf("读取帧数据成功，通道 0 前 8 点：");
        for (int i = 0; i < 8; ++i)
            printf("%.6f ", data[i]);
        printf("\n");
    }
    else if (command_in == "frame_date")
    {
        if (argc < 3)
        {
            fprintf(stderr, "[WARN] frame_date 需要帧号，例如 frame_date 0\n");
            return -1;
        }
        const int64_t idx = strtoll(argv[2], nullptr, 10);
        make_path(path_buf, sizeof(path_buf), (argc >= 4) ? atoi(argv[3]) : FILE_START);

        string buf;
        if (bin_read_time(path_buf, idx, buf) != 0)
        {
            printf("读取帧日期时间失败\n");
            return -1;
        }
        printf("%s\n", buf.c_str());
    }

    else
    {
        printf("未知命令：%s\n", argv[1]);
        usage();
        return -1;
    }

    return 0;
}
