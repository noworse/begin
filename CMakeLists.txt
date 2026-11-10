# =============================================================================
# 工程：BEG（bin 读取与周期定位）
# 结构：src/function  BCD 时间戳解码        -> 静态库 libfunc.a
#       src/io        bin 读取与周期定位    -> 静态库 libbinio.a
#       test          手工测试程序          -> 可执行文件 test_read
# 产物：build/lib/*.a、build/test_read
# =============================================================================

# ---------------------------- 一、编译环境 ----------------------------

#规定Cmake版本号
cmake_minimum_required(VERSION 3.18)
#设置项目名及语言
# project(PRO VERSION 1.0.0 LANGUAGES CUDA CXX)
project(BEG VERSION 1.0.0 LANGUAGES CXX)
#设置C标准
set(CMAKE_CXX_STANDARD  20)
#强制使用设置的标准
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# 导出 compile_commands.json，供 VS Code IntelliSense 复用真实编译参数
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
#添加编译选项
add_compile_options(-Wall -Wextra -O2)
# #设置cuda编译版本
# set(CMAKE_CUDA_ARCHITECTURES 87)
# ------------------------ 二、产物路径与目标名 ------------------------

#开启ctest
enable_testing()


# ---------------------------- 三、目录与链接 ----------------------------

#设置库路径
set(LIB_PATH ${CMAKE_BINARY_DIR}/lib)
#设置可执行文件路径
# set(EXEC_PATH ${CMAKE_BINARY_DIR}/bin)
set(EXEC_PATH ${CMAKE_BINARY_DIR})
# #设置头文件路径
# set(HEAD_PATH ${CMAKE_CURRENT_SOURCE_DIR}/include)


#设置目标所需文件夹以编译库
set(FUNC_LIB func)
set(IO_LIB binio)
#设置最终生成可执行文件别名
set(TEST test_read)
#添加子源文件夹
#顺序有依赖：io 链接 func，test 链接 io 与 func，故 io 必须在 func 之后

add_subdirectory(src/function)
add_subdirectory(src/io)
add_subdirectory(test)