include_guard(GLOBAL)
get_filename_component(WA2_NATIVE_HOST_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
if(NOT TARGET wa2_native_application)
    message(FATAL_ERROR "The native SDL host requires the common native application")
endif()
if(NOT TARGET wa2_canvas)
    include("${WA2_NATIVE_HOST_ROOT}/platform/sdl_canvas.cmake")
endif()
add_library(wa2_native_host STATIC "${WA2_NATIVE_HOST_ROOT}/platform/native_host.cpp"
    "${WA2_NATIVE_HOST_ROOT}/platform/sdl_application.cpp" "${WA2_NATIVE_HOST_ROOT}/platform/sdl_presentation.cpp"
    "${WA2_NATIVE_HOST_ROOT}/platform/sdl_audio.cpp")
target_compile_features(wa2_native_host PUBLIC cxx_std_20)
target_link_libraries(wa2_native_host PUBLIC wa2_native_application wa2_native_audio_binding wa2_canvas)
