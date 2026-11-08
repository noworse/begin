# 收集当前目录下所有 cpp（与 src/io 保持同样风格）
file(GLOB SRC CONFIGURE_DEPENDS "./*.cpp")
# 编译成静态库 ${FUNC_LIB}
add_library(${FUNC_LIB} STATIC ${SRC})
# 把本目录加入头文件搜索路径（PUBLIC -> 依赖它的目标也能直接 include 本目录头文件）
target_include_directories(${FUNC_LIB} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# 产物放到统一 lib 目录
set_target_properties(${FUNC_LIB} PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${LIB_PATH})
