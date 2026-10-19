# Pinned, unmodified upstream archives are recorded in tools/sdl-graphics-dependencies.json.
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_DIRECTX OFF CACHE BOOL "" FORCE)
set(SDL_RENDER_D3D OFF CACHE BOOL "" FORCE)
set(SDL_RENDER OFF CACHE BOOL "" FORCE)
set(SDL_AUDIO ON CACHE BOOL "" FORCE)
set(SDL_DISKAUDIO OFF CACHE BOOL "" FORCE)
set(SDL_WASAPI ON CACHE BOOL "" FORCE)
set(SDL_JOYSTICK OFF CACHE BOOL "" FORCE)
set(SDL_HAPTIC OFF CACHE BOOL "" FORCE)
set(SDL_SENSOR OFF CACHE BOOL "" FORCE)
set(SDL_HIDAPI OFF CACHE BOOL "" FORCE)
set(SDL_VULKAN OFF CACHE BOOL "" FORCE)
set(SDL_OPENGLES OFF CACHE BOOL "" FORCE)
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../tools/SDL-release-2.32.10" sdl2 EXCLUDE_FROM_ALL)

set(wa2_mojo "${CMAKE_CURRENT_LIST_DIR}/../tools/mojoshader-main")
set(wa2_assembler "${CMAKE_CURRENT_LIST_DIR}/../tools/mojoshader_assembler-main")
file(GLOB wa2_mojo_profiles CONFIGURE_DEPENDS "${wa2_mojo}/profiles/*.c")
add_library(wa2_mojoshader STATIC "${wa2_mojo}/mojoshader.c" "${wa2_mojo}/mojoshader_common.c" "${wa2_mojo}/mojoshader_opengl.c" ${wa2_mojo_profiles})
target_include_directories(wa2_mojoshader PUBLIC "${wa2_mojo}")
target_compile_definitions(wa2_mojoshader PUBLIC MOJOSHADER_NO_VERSION_INCLUDE PRIVATE
    SUPPORT_PROFILE_D3D=0 SUPPORT_PROFILE_HLSL=0 SUPPORT_PROFILE_GLSLES=0 SUPPORT_PROFILE_GLSLES3=0
    SUPPORT_PROFILE_ARB1=0 SUPPORT_PROFILE_ARB1_NV=0 SUPPORT_PROFILE_METAL=0 SUPPORT_PROFILE_SPIRV=0 SUPPORT_PROFILE_GLSPIRV=0
    MOJOSHADER_FLIP_RENDERTARGET MOJOSHADER_DEPTH_CLIPPING)
add_library(wa2_shader_assembler STATIC
    "${wa2_assembler}/mojoshader_assembler_common.c" "${wa2_assembler}/mojoshader_preprocessor.c"
    "${wa2_assembler}/mojoshader_lexer.c" "${wa2_assembler}/mojoshader_assembler.c")
target_include_directories(wa2_shader_assembler PUBLIC "${wa2_assembler}")
target_compile_definitions(wa2_shader_assembler PUBLIC MOJOSHADER_ASSEMBLER_NO_VERSION_INCLUDE)
target_link_libraries(wa2_shader_assembler PUBLIC wa2_mojoshader)
# The upstream assembler split retains private common symbols also exported by
# the translator. Keep its copy private without modifying either source archive.
foreach(symbol MOJOSHADER_internal_malloc MOJOSHADER_internal_free
    stringcache stringcache_len stringcache_iscached stringcache_fmt stringcache_create stringcache_destroy
    errorlist_create errorlist_add errorlist_add_fmt errorlist_add_va errorlist_count errorlist_flatten errorlist_destroy
    buffer_create buffer_reserve buffer_append buffer_append_fmt buffer_append_va buffer_size buffer_flatten buffer_empty buffer_destroy buffer_merge buffer_find buffer_patch
    MOJOSHADER_printFloat MOJOSHADER_out_of_mem_error MOJOSHADER_out_of_mem_data)
    target_compile_definitions(wa2_shader_assembler PRIVATE "${symbol}=WA2_assembler_${symbol}")
endforeach()
if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    # Upstream passes NULL for a Win32 DWORD flag (equivalent to zero).
    target_compile_options(wa2_shader_assembler PRIVATE -Wno-int-conversion)
endif()
