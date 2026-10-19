# The serialized sound adapter and PCM renderer are the same on every host.
# Reuse the already built SDL backend for the isolated core/link check; a full
# host build supplies its normal SDL2 target instead.
if(TARGET SDL2)
    set(WA2_SOUND_SDL_TARGET SDL2)
elseif(TARGET SDL2-static)
    set(WA2_SOUND_SDL_TARGET SDL2-static)
else()
    if(NOT TARGET wa2_sdl_host)
        if(ANDROID)
            add_library(wa2_sdl_host SHARED IMPORTED GLOBAL)
            set_target_properties(wa2_sdl_host PROPERTIES IMPORTED_LOCATION
                "${WA2_SHARED_ROOT}/build/android-native-game/sdl2/libSDL2.so")
        elseif(WIN32)
            add_library(wa2_sdl_host STATIC IMPORTED GLOBAL)
            set_target_properties(wa2_sdl_host PROPERTIES IMPORTED_LOCATION
                "${WA2_SHARED_ROOT}/build/wa2-win64/wa2_port/sdl2/libSDL2.a")
            set_property(TARGET wa2_sdl_host PROPERTY INTERFACE_LINK_LIBRARIES
                "setupapi;winmm;imm32;version;ole32;oleaut32;uuid;advapi32;user32;gdi32;shell32")
        else()
            message(FATAL_ERROR "Supply the host's SDL2 target before adding the shared runtime")
        endif()
        set_target_properties(wa2_sdl_host PROPERTIES INTERFACE_INCLUDE_DIRECTORIES
            "${WA2_SHARED_ROOT}/tools/SDL-release-2.32.10/include")
    endif()
    set(WA2_SOUND_SDL_TARGET wa2_sdl_host)
endif()
add_library(wa2_shared_audio STATIC
    "${WA2_SHARED_ROOT}/port/audio.cpp"
    "${WA2_SHARED_ROOT}/platform/sdl_audio.cpp")
target_compile_features(wa2_shared_audio PUBLIC cxx_std_20)
target_include_directories(wa2_shared_audio PUBLIC
    "${WA2_SHARED_ROOT}/tools/WindowsRecompTool-main/include"
    "${WA2_SHARED_ROOT}/engine" "${WA2_SHARED_ROOT}/platform")
target_compile_definitions(wa2_shared_audio PUBLIC WA2_STATIC_FUNCTIONS_ONLY=1)
target_link_libraries(wa2_shared_audio PUBLIC wa2_engine ${WA2_SOUND_SDL_TARGET})
set_target_properties(wa2_shared_audio PROPERTIES POSITION_INDEPENDENT_CODE ON)
