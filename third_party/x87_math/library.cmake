# Shared scalar arithmetic only; CPU/device/decoder sources are not included.
if(TARGET wa2_x87_transcend)
    return()
endif()
get_filename_component(WA2_X87_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(WA2_X87_MATH "${CMAKE_CURRENT_LIST_DIR}")
file(GLOB WA2_X87_SOURCES CONFIGURE_DEPENDS
    "${WA2_X87_MATH}/softfloat3e/*.cc"
    "${WA2_X87_MATH}/softfloat3e/8086-SSE/*.cc")
list(APPEND WA2_X87_SOURCES
    "${WA2_X87_MATH}/fpu/f128_67.cc" "${WA2_X87_MATH}/fpu/fprem.cc"
    "${WA2_X87_MATH}/fpu/fyl2x.cc" "${WA2_X87_MATH}/fpu/f2xm1.cc"
    "${WA2_X87_MATH}/fpu/fpatan.cc" "${WA2_X87_MATH}/fpu/fsincos.cc"
    "${WA2_X87_ROOT}/recompiled/transcend.cpp")
add_library(wa2_x87_transcend STATIC ${WA2_X87_SOURCES})
target_include_directories(wa2_x87_transcend PRIVATE "${WA2_X87_MATH}"
    "${WA2_X87_MATH}/softfloat3e/include" "${WA2_X87_ROOT}/recompiled")
target_compile_features(wa2_x87_transcend PUBLIC cxx_std_20)
set_target_properties(wa2_x87_transcend PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(wa2_x87_transcend PRIVATE -O2)
endif()
