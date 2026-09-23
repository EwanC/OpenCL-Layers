# Copyright (c) 2025-2026 Ewan Crawford

if (CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    add_compile_options(/WX /EHsc)
else()
    add_compile_options(-Wall -Wextra -Werror)
endif()

set(SOURCES
    command_buffer_record.cpp
    command_buffer_record.h
    ${PROJECT_SOURCE_DIR}/include/cl_layer_command_buffer_record.h)
if (WIN32)
    list(APPEND SOURCES command_buffer_record.def)
endif()

add_library(CLRecordLayer SHARED ${SOURCES})

target_compile_definitions(CLRecordLayer PRIVATE CL_TARGET_OPENCL_VERSION=300)
target_include_directories(CLRecordLayer PRIVATE ${OpenCLIncludeDir})
target_include_directories(CLRecordLayer PRIVATE ${PROJECT_SOURCE_DIR}/include)
set_target_properties(CLRecordLayer PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib
    VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}"
    SOVERSION "${PROJECT_VERSION_MAJOR}")

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
install(FILES "${PROJECT_SOURCE_DIR}/include/cl_layer_command_buffer_record.h"
              DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/CL")
