cmake_minimum_required(VERSION 3.20)
project(WA2SharedAotAndroidCheck LANGUAGES CXX)
if(NOT ANDROID_ABI STREQUAL "arm64-v8a")
    message(FATAL_ERROR "This check must use the Android ARM64 toolchain")
endif()
add_subdirectory(../../recompiled shared_aot)
add_library(wa2_shared_aot_link_check SHARED entry.cpp)
get_filename_component(WA2_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
add_library(wa2_android_resource_bridge STATIC
    "${WA2_ROOT}/android/native/android_resources.cpp"
    "${WA2_ROOT}/android/native/compiled_resources.cpp")
target_include_directories(wa2_android_resource_bridge PRIVATE
    "${WA2_ROOT}/tools/SDL-release-2.32.10/include" "${WA2_ROOT}/engine" "${WA2_ROOT}/platform"
    "${WA2_ROOT}/tools/WindowsRecompTool-main/include")
target_compile_features(wa2_android_resource_bridge PUBLIC cxx_std_20)
set_target_properties(wa2_android_resource_bridge PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(wa2_shared_aot_link_check PRIVATE
    "-Wl,--whole-archive" wa2_function_process wa2_shared_aot wa2_initial_data wa2_aot_memory wa2_platform wa2_engine wa2_shared_audio wa2_android_resource_bridge wa2_native_widgets "-Wl,--no-whole-archive" wa2_sdl_host)
target_link_options(wa2_shared_aot_link_check PRIVATE "-Wl,--no-undefined" "-Wl,-z,max-page-size=16384" "-Wl,--exclude-libs,ALL" "-Wl,-Bsymbolic")
add_subdirectory("${WA2_ROOT}/hosts" actual_host)
