cmake_minimum_required(VERSION 3.25)
project(stratforge VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Options
option(SF_BUILD_TESTS "Build tests" ON)
option(SF_BUILD_EXAMPLES "Build examples" ON)
option(SF_BUILD_BENCHMARKS "Build benchmarks" ON)

# Dependencies via FetchContent
include(FetchContent)

# Platform-specific compiler flags
if(MSVC)
    add_compile_options(
        /W4
        /WX
        /permissive-
        /Zc:__cplusplus
        /utf-8
    )

    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(/O2 /GL)
        add_link_options(/LTCG)
    endif()
else()
    # Clang on Linux: use libc++ for full C++23 support
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_compile_options(-stdlib=libc++)
        add_link_options(-stdlib=libc++ -lc++abi)
    endif()

    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        -Wno-unused-parameter
    )

    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-O3)
        if(NOT DEFINED ENV{CI})
            add_compile_options(-march=native)
        endif()
        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            add_compile_options(-flto=auto -fno-fat-lto-objects)
            add_link_options(-flto=auto -fuse-linker-plugin)
        elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            add_compile_options(-flto=thin)
            add_link_options(-flto=thin)
        endif()
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()
endif()

# Main header-only library
add_library(stratforge INTERFACE)
target_include_directories(stratforge INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_features(stratforge INTERFACE cxx_std_23)

# Tests
if(SF_BUILD_TESTS)
    enable_testing()
    find_package(Catch2 3 QUIET)
    if(NOT Catch2_FOUND)
        message(STATUS "Catch2 not found, fetching via FetchContent...")
        FetchContent_Declare(
            Catch2
            GIT_REPOSITORY https://github.com/catchorg/Catch2.git
            GIT_TAG v3.5.2
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(Catch2)
        list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
    endif()
    add_subdirectory(tests)
endif()

# Examples
if(SF_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Benchmarks
if(SF_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

# Install headers
install(DIRECTORY include/stratforge
    DESTINATION include
)
