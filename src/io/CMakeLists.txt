#指定收集文件到SRC(当前文件夹)
file(GLOB SRC CONFIGURE_DEPENDS "./*.cpp")
#用SRC里面的所有源文件，编译生成一个名叫${FUNC_LIB}的静态库目标。
add_library(${IO_LIB} STATIC ${SRC})
#链接头文件，
target_include_directories(${IO_LIB} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
#指定静态库编译出来的库放到${}目录下
set_target_properties(${IO_LIB} PROPERTIES
ARCHIVE_OUTPUT_DIRECTORY ${LIB_PATH})

# 链接 func 静态库：bcd_date_to_integer 等实现在 src/fuction/function.cpp
# PUBLIC 会把它 target_include_directories 里登记的目录一并传出去，
# 所以本目录的源文件可以直接 #include "function.hpp"
target_link_libraries(${IO_LIB} PUBLIC ${FUNC_LIB})
