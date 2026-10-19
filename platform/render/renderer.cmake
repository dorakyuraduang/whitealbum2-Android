# Both hosts compile one source ABI, state cache, shader and GPU submission path.
if(NOT TARGET wa2_fixed_pipeline)
    include("${CMAKE_CURRENT_LIST_DIR}/shaders.cmake")
endif()
add_library(wa2_shared_graphics STATIC
    "${CMAKE_CURRENT_LIST_DIR}/source_graphics.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/movie_layout.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/frame_layout.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/surface_host.cpp")
target_compile_features(wa2_shared_graphics PUBLIC cxx_std_20)
target_compile_definitions(wa2_shared_graphics PUBLIC WA2_STATIC_FUNCTIONS_ONLY=1)
target_include_directories(wa2_shared_graphics PUBLIC
    "${WA2_SHARED_ROOT}/tools/WindowsRecompTool-main/include")
target_link_libraries(wa2_shared_graphics PUBLIC
    wa2_fixed_pipeline wa2_common_shader_assembler wa2_common_shader_gl wa2_pixel_transfer ${WA2_SOUND_SDL_TARGET})
set_target_properties(wa2_shared_graphics PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    set_source_files_properties("${WA2_SHARED_ROOT}/platform/render/source_graphics.cpp" PROPERTIES COMPILE_OPTIONS "-O2")
endif()
