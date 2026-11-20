cmake_minimum_required(VERSION 3.20)
project(WA2TranscendentalMemoryChecks LANGUAGES CXX)
get_filename_component(WA2_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
include("${WA2_ROOT}/third_party/x87_math/library.cmake")
add_executable(wa2_transcendental_tests "${WA2_ROOT}/tests/transcendental.cpp"
    "${WA2_ROOT}/recompiled/extended80.cpp")
target_compile_features(wa2_transcendental_tests PRIVATE cxx_std_20)
target_include_directories(wa2_transcendental_tests PRIVATE "${WA2_ROOT}/recompiled"
    "${WA2_ROOT}/tools/WindowsRecompTool-main/include")
target_link_libraries(wa2_transcendental_tests PRIVATE wa2_x87_transcend)
