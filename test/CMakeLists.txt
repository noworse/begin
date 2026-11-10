# =============================================================================
# 目标：test_read —— 手工测试入口，见 read_one.cpp 顶部的命令清单
# 产物：放到 ${EXEC_PATH}（build 根目录），运行方式 ./build/test_read cycles
# =============================================================================

# ---------------------------- 一、可执行文件 ----------------------------

add_executable(${TEST} read_one.cpp)
target_link_libraries(${TEST} PRIVATE ${IO_LIB})
target_link_libraries(${TEST} PRIVATE ${FUNC_LIB})

#指定编译出来的文件放到${}目录下
set_target_properties(${TEST} PROPERTIES
RUNTIME_OUTPUT_DIRECTORY ${EXEC_PATH})

