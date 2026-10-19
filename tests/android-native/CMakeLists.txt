cmake_minimum_required(VERSION 3.22)
project(WA2AndroidNativeCheck LANGUAGES C CXX)
if(NOT ANDROID OR NOT ANDROID_ABI STREQUAL "arm64-v8a")
    message(FATAL_ERROR "This is an Android NDK ARM64 link check, not a desktop game or APK")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_filename_component(WA2_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_OPENGLES ON CACHE BOOL "" FORCE)
set(SDL_OPENGL OFF CACHE BOOL "" FORCE)
set(SDL_RENDER ON CACHE BOOL "" FORCE)
set(SDL_DIRECTX OFF CACHE BOOL "" FORCE)
set(SDL_RENDER_D3D OFF CACHE BOOL "" FORCE)
set(SDL_VULKAN OFF CACHE BOOL "" FORCE)
set(SDL_HIDAPI OFF CACHE BOOL "" FORCE)
# SDL2's Android JNI exports reference these backends even for touch-only
# applications. Keep them enabled, matching SDL's Android defaults.
set(SDL_JOYSTICK ON CACHE BOOL "" FORCE)
set(SDL_HAPTIC ON CACHE BOOL "" FORCE)
set(SDL_SENSOR OFF CACHE BOOL "" FORCE)
add_subdirectory("${WA2_ROOT}/tools/SDL-release-2.32.10" sdl2 EXCLUDE_FROM_ALL)
target_link_options(SDL2 PRIVATE "-Wl,-z,max-page-size=16384")
add_subdirectory("${WA2_ROOT}/engine" engine EXCLUDE_FROM_ALL)
include("${WA2_ROOT}/platform/portable.cmake")
include("${WA2_ROOT}/platform/sdl_canvas.cmake")
include("${WA2_ROOT}/engine/movie.cmake")
add_library(wa2_sdl_audio STATIC "${WA2_ROOT}/platform/sdl_audio.cpp")
target_link_libraries(wa2_sdl_audio PUBLIC wa2_engine PRIVATE SDL2)
target_include_directories(wa2_sdl_audio PUBLIC "${WA2_ROOT}/platform")
add_library(wa2_android_native_check SHARED
    "${WA2_ROOT}/android/native/frame_metrics.cpp"
    "${WA2_ROOT}/android/native/frame_metrics_jni.cpp"
    "${WA2_ROOT}/android/native/android_resources.cpp")
target_include_directories(wa2_android_native_check PRIVATE "${WA2_ROOT}/android/native")
# Include every migrated module so an unused archive member cannot conceal a
# Windows-only dependency. This check deliberately has no game entry point.
target_link_libraries(wa2_android_native_check PRIVATE
    "-Wl,--whole-archive" wa2_script wa2_canvas wa2_sdl_audio wa2_movie wa2_platform wa2_engine
    "-Wl,--no-whole-archive" SDL2 log android)
target_link_options(wa2_android_native_check PRIVATE "-Wl,--no-undefined" "-Wl,-z,max-page-size=16384" "-Wl,-Bsymbolic")
