# Copyright (c) 2025-2026 Ewan Crawford

cmake_minimum_required(VERSION 3.20 FATAL_ERROR)
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

add_subdirectory(source)
add_subdirectory(test)
