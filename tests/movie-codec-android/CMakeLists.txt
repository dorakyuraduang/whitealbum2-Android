cmake_minimum_required(VERSION 3.20)
project(WA2MovieCodecMemoryCheck LANGUAGES CXX)
if(NOT ANDROID)
    message(FATAL_ERROR "Use the Android NDK toolchain for this memory-only check")
endif()
get_filename_component(WA2_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
# Reader only needs Source's interface. No game, AOT functions, guest state,
# application entry point, renderer or device backend is linked into this CLI.
add_library(wa2_engine INTERFACE)
include("${WA2_ROOT}/engine/movie.cmake")
add_executable(wa2_movie_codec_probe "${WA2_ROOT}/tests/movie_codecs.cpp" "${WA2_ROOT}/platform/file_io.cpp")
target_link_libraries(wa2_movie_codec_probe PRIVATE wa2_movie)
