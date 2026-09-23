add_library (CLCommandBufferRecordLayer SHARED
    command_buffer_record.cpp
    command_buffer_record.hpp
    cl_layer_command_buffer_record.h
#   PLATFORM_ID taking a comma-separated list is CMake 3.15
#   $<$<AND:$<PLATFORM_ID:Windows>,$<CXX_COMPILER_ID:MSVC,Clang>>:command_buffer_record.def>
    $<$<AND:$<PLATFORM_ID:Windows>,$<OR:$<CXX_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:Clang>>>:command_buffer_record.def>
    $<$<CXX_COMPILER_ID:GNU>:command_buffer_record.map>
)

target_link_libraries (CLCommandBufferRecordLayer PRIVATE LayersCommon)

target_include_directories (CLCommandBufferRecordLayer
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

if (NOT WIN32 AND NOT APPLE)
    set_target_properties (CLCommandBufferRecordLayer PROPERTIES LINK_FLAGS "-Wl,--version-script -Wl,${CMAKE_CURRENT_SOURCE_DIR}/command_buffer_record.map")
endif ()

set (INSTALL_TARGETS CLCommandBufferRecordLayer)
set (BUILD_TARGETS ${INSTALL_TARGETS})

set_target_properties (${BUILD_TARGETS}
    PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR}"
        LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR}"
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}"
        PDB_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}"
        INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}"
        FOLDER "Layers"
)

install (
    TARGETS ${INSTALL_TARGETS}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install (
    FILES cl_layer_command_buffer_record.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/CL
)
