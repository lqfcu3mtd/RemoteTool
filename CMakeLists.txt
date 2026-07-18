cmake_minimum_required(VERSION 3.24)

project(RemoteTool
    LANGUAGES CXX
    VERSION 0.1.0
    DESCRIPTION "Remote Maintenance Tunnel Tool"
)

# --- C++ 标准 ---
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- 编译选项 ---
if(MSVC)
    add_compile_options(/W4 /permissive- /EHsc)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    # Release 优化
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(/O2)
    endif()
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-O2)
    endif()
endif()

# --- 输出目录 ---
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# --- 第三方依赖（Phase 0 占位，后续固定版本） ---
include(${CMAKE_SOURCE_DIR}/cmake/Dependencies.cmake)

# --- rmt_core 静态库 ---
add_library(rmt_core STATIC
    src/protocol/frame.cpp
)

target_include_directories(rmt_core
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)

# --- 测试 ---
enable_testing()

# frame_test
add_executable(frame_test tests/unit/frame_test.cpp)
target_link_libraries(frame_test PRIVATE rmt_core)
add_test(NAME frame_test COMMAND frame_test)
