cmake_minimum_required(VERSION 3.16)
project(nds_dump CXX C)

set(CMAKE_CXX_STANDARD 20)
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# melonDS's core library alone: no Qt frontend, no OpenGL, no GDB stub.
set(BUILD_QT_SDL OFF CACHE BOOL "" FORCE)
set(ENABLE_OGLRENDERER OFF CACHE BOOL "" FORCE)
set(ENABLE_GDBSTUB OFF CACHE BOOL "" FORCE)
get_filename_component(MELONDS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/melonds" ABSOLUTE)
include("${MELONDS_DIR}/../patches/apply_melonds_patch.cmake")
add_subdirectory("${MELONDS_DIR}" melonds EXCLUDE_FROM_ALL)

find_package(ZLIB REQUIRED)

add_executable(nds_dump
    nds_dump.cpp
    ../../app/src/main/cpp/nds/pk_nds_platform.cpp
    ../../app/src/main/cpp/nds/pk_nds_field.cpp
    ../../app/src/main/cpp/nds/pk_nds_call.cpp
    ../../app/src/main/cpp/nds/pk_nds_wide.cpp
    ../../app/src/main/cpp/nds/pk_nds_music.cpp)
target_include_directories(nds_dump PRIVATE ../../app/src/main/cpp/nds)
target_link_libraries(nds_dump PRIVATE core ZLIB::ZLIB)
