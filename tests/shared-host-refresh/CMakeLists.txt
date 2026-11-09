cmake_minimum_required(VERSION 3.20)
project(WA2SharedHostRefresh LANGUAGES C CXX)
get_filename_component(WA2_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
# Exercise the shipped, unmetered static bodies. Historical comparison builds
# require deleted baseline/oracle objects and are not silently substituted.
set(WA2_SHARED_AOT_CHECKS OFF CACHE BOOL "" FORCE)
set(WA2_AOT_DIAGNOSTICS OFF CACHE BOOL "" FORCE)
set(CMAKE_CXX_STANDARD 20)
add_subdirectory("${WA2_ROOT}/recompiled" shared_aot)
foreach(name selected_save_files portable_system resource_mount test_profile portable_scheduler function_process subtitles portable_shell fast_forward script_wait_callback save_inventory profile_batch io_diagnostics resource_reuse resource_loading host_progress)
    add_executable(wa2_refresh_${name} "${WA2_ROOT}/tests/${name}.cpp")
    target_link_libraries(wa2_refresh_${name} PRIVATE wa2_function_process)
endforeach()
