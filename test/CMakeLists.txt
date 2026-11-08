add_executable(${TEST} read_one.cpp)
target_link_libraries(${TEST} PRIVATE ${IO_LIB})

#指定编译出来的文件放到${}目录下
set_target_properties(${TEST} PROPERTIES
RUNTIME_OUTPUT_DIRECTORY ${EXEC_PATH})

