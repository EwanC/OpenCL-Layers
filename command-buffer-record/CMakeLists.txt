# Copyright (c) 2024 The Khronos Group Inc.
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20 FATAL_ERROR)

# This directory is self contained so that it can be used either as the root of
# a standalone repository, or as a sub-directory of a larger project such as the
# OpenCL-Layers collection.
if (CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(REC_STANDALONE_BUILD ON)
else()
    set(REC_STANDALONE_BUILD OFF)
endif()

if (REC_STANDALONE_BUILD)
    project(CLRecordLayer VERSION 0.1.0 LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)

    # Project defined CMake options
    option(REC_DEBUG "Enable CLRecordLayer debug logging to stderr" OFF)
    option(REC_OFFLINE_BUILD "Avoids pulling dependencies from GitHub" OFF)

    include(FetchContent)
    if (REC_OFFLINE_BUILD)
        find_package(OpenCLHeaders REQUIRED)
        get_target_property(OpenCLIncludeDir OpenCL::Headers INTERFACE_INCLUDE_DIRECTORIES)
    else()
        FetchContent_Declare(OpenCL-Headers
            GIT_REPOSITORY "https://github.com/KhronosGroup/OpenCL-Headers.git"
            GIT_TAG main)
        FetchContent_MakeAvailable(OpenCL-Headers)
        FetchContent_GetProperties(OpenCL-Headers SOURCE_DIR OpenCLIncludeDir)
    endif()
    message(STATUS "OpenCL Include Directory: ${OpenCLIncludeDir}")
endif()

add_subdirectory(source)

if (REC_STANDALONE_BUILD)
    add_subdirectory(test)
endif()
