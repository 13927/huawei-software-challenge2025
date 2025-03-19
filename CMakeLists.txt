cmake_minimum_required(VERSION 3.8)  # 不要修改
project(CodeCraft)                   # 不要修改

set(CMAKE_CXX_STANDARD      17)      # 不要修改
set(CMAKE_C_STANDARD        11)      # 不要修改

set(EXECUTABLE_OUTPUT_PATH  ${PROJECT_SOURCE_DIR}/) # 不要修改
option(BUILD_SHARED_LIBS    ""  OFF) # 不要修改

aux_source_directory(./                     cur_src) # 不要修改
aux_source_directory(./src                  src_dir) # 增加 src 目录

# 直接设置 CMAKE_CXX_FLAGS 和 CMAKE_C_FLAGS，开启最高优化
set(CMAKE_CXX_FLAGS "-O3 -march=native -DNDEBUG")
set(CMAKE_C_FLAGS "-O3 -march=native -DNDEBUG")

add_executable(code_craft                   ${cur_src} ${src_dir}) # 不要修改 code_craft 名称

# 以下可以根据需要增加需要链接的库
#if (NOT WIN32)
#    target_link_libraries(code_craft  pthread  rt  m)
#endif (NOT WIN32)