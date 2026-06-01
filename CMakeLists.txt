cmake_minimum_required(VERSION 3.10)
project(librpc2 C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_FIND_LIBRARY_SUFFIXES ".a" ".so")

find_package(PkgConfig REQUIRED)
pkg_check_modules(JSON_C REQUIRED IMPORTED_TARGET json-c)
find_library(UBOX ubox REQUIRED)

add_library(rpc2 STATIC
    src/rpc2_ctx.c
    src/rpc2_request.c
    src/rpc2_timer.c
    src/rpc2_process.c
    src/rpc2_framing.c
)

target_include_directories(rpc2
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/rpc2>
        $<INSTALL_INTERFACE:include/rpc2>
)

target_link_libraries(rpc2
    PUBLIC
        PkgConfig::JSON_C
        ${UBOX}
)

target_compile_features(rpc2 PRIVATE c_std_11)
target_compile_definitions(rpc2 PRIVATE _GNU_SOURCE)
