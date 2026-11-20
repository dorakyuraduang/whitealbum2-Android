# Both platforms compile this exact catalogue and the existing portable kernels.
include("${WA2_SHARED_ROOT}/generated/special-contents/aot/sources.cmake")
add_library(wa2_special_aot STATIC ${WA2_SPECIAL_AOT_SOURCES}
    "${WA2_SHARED_ROOT}/recompiled/extended80.cpp"
    "${WA2_SHARED_ROOT}/recompiled/native_string.cpp"
    "${WA2_SHARED_ROOT}/recompiled/transcend.cpp")
target_compile_features(wa2_special_aot PUBLIC cxx_std_20)
target_include_directories(wa2_special_aot PUBLIC
    "${WA2_SHARED_ROOT}/generated/special-contents/aot"
    "${WA2_SHARED_ROOT}/recompiled"
    "${WA2_SHARED_ROOT}/tools/WindowsRecompTool-main/include")
target_link_libraries(wa2_special_aot PUBLIC wa2_aot_memory wa2_scalar_math)
set_target_properties(wa2_special_aot PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(wa2_special_process STATIC runtime.cpp storage.cpp
    "${WA2_SHARED_ROOT}/platform/compat/extended_system.cpp")
target_compile_features(wa2_special_process PUBLIC cxx_std_20)
target_link_libraries(wa2_special_process PUBLIC wa2_special_aot wa2_function_process)
target_include_directories(wa2_special_process PUBLIC "${CMAKE_CURRENT_LIST_DIR}")
set_target_properties(wa2_special_process PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(wa2_special_aot PRIVATE -Oz)
endif()
option(WA2_SPECIAL_CHECKS "Build finite memory-only Special checks" OFF)
if(WA2_SPECIAL_CHECKS AND NOT ANDROID)
    add_executable(wa2_special_memory_tests
        "${WA2_SHARED_ROOT}/tests/special_aot.cpp"
        "${WA2_SHARED_ROOT}/generated/special-contents/aot/probe_oracle.cpp")
    target_link_libraries(wa2_special_memory_tests PRIVATE wa2_special_aot)
    add_executable(wa2_special_runtime_tests "${WA2_SHARED_ROOT}/tests/special_runtime.cpp")
    target_link_libraries(wa2_special_runtime_tests PRIVATE wa2_special_process)
endif()
