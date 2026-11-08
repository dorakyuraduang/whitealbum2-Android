include_guard(GLOBAL)
get_filename_component(WA2_NATIVE_ANDROID_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
function(wa2_add_native_android_entry target)
    if(NOT ANDROID OR NOT TARGET SDL2 OR NOT TARGET wa2_native_application)
        message(FATAL_ERROR "The native Android entry requires Android, SDL2 and the shared native application")
    endif()
    if(NOT TARGET wa2_canvas)
        include("${WA2_NATIVE_ANDROID_ROOT}/platform/sdl_canvas.cmake")
    endif()
    include("${WA2_NATIVE_ANDROID_ROOT}/platform/native_host.cmake")
    add_library(${target} SHARED
        "${WA2_NATIVE_ANDROID_ROOT}/android/game/game_host.cpp"
        "${WA2_NATIVE_ANDROID_ROOT}/android/game/game_jni.cpp"
        "${WA2_NATIVE_ANDROID_ROOT}/android/game/user_menu_jni.cpp"
        "${WA2_NATIVE_ANDROID_ROOT}/android/native/android_entry.cpp"
        "${WA2_NATIVE_ANDROID_ROOT}/android/native/android_resources.cpp"
        "${WA2_NATIVE_ANDROID_ROOT}/android/native/frame_metrics.cpp"
        "${WA2_NATIVE_ANDROID_ROOT}/android/native/frame_hud_jni.cpp"
        "${WA2_NATIVE_ANDROID_ROOT}/platform/resource_fd.cpp")
    target_include_directories(${target} PRIVATE "${WA2_NATIVE_ANDROID_ROOT}/android/native")
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_compile_definitions(${target} PRIVATE WA2_NATIVE_GAME_STORAGE=1)
    target_link_libraries(${target} PRIVATE wa2_native_host SDL2 log android)
    target_link_options(${target} PRIVATE "-Wl,--no-undefined" "-Wl,-z,max-page-size=16384" "-Wl,--exclude-libs,ALL" "-Wl,-Bsymbolic")
endfunction()
