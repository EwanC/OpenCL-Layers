# Copyright (c) 2024 The Khronos Group Inc.
# SPDX-License-Identifier: Apache-2.0

if (CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    add_compile_options(/WX /EHsc)
else()
    add_compile_options(-Wall -Wextra -Werror)
endif()

set(SOURCES
    command_buffer_record.cpp
    command_buffer_record.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/cl_layer_command_buffer_record.h)
if (WIN32)
    list(APPEND SOURCES command_buffer_record.def)
endif()

add_library(CLRecordLayer SHARED ${SOURCES})

target_include_directories(CLRecordLayer
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)

if (REC_STANDALONE_BUILD)
    target_compile_definitions(CLRecordLayer PRIVATE CL_TARGET_OPENCL_VERSION=300)
    target_include_directories(CLRecordLayer PRIVATE ${OpenCLIncludeDir})
    set_target_properties(CLRecordLayer PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib
        VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}"
        SOVERSION "${PROJECT_VERSION_MAJOR}")
else()
    # Built as part of the OpenCL-Layers collection, which provides the OpenCL
    # headers and the common layer compile definitions.
    target_link_libraries(CLRecordLayer PRIVATE LayersCommon)
    target_compile_features(CLRecordLayer PRIVATE cxx_std_17)
    set_target_properties(CLRecordLayer PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR}"
        LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR}"
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}"
        FOLDER "Layers")
endif()

if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set_target_properties(CLRecordLayer PROPERTIES
        LINK_FLAGS "-Wl,--version-script -Wl,${CMAKE_CURRENT_SOURCE_DIR}/command_buffer_record.map")
endif()

if (REC_DEBUG)
    target_compile_definitions(CLRecordLayer PRIVATE REC_DEBUG)
endif()

include(GNUInstallDirs)
install(TARGETS CLRecordLayer
                ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
                RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
                LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../include/cl_layer_command_buffer_record.h"
              DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/CL")
