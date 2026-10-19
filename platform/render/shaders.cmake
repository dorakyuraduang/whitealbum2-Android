get_filename_component(WA2_SHADER_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(wa2_mojo "${WA2_SHADER_ROOT}/tools/mojoshader-main")
set(wa2_assembler "${WA2_SHADER_ROOT}/tools/mojoshader_assembler-main")
file(GLOB wa2_mojo_profiles CONFIGURE_DEPENDS "${wa2_mojo}/profiles/*.c")
add_library(wa2_common_shader_parser STATIC "${wa2_mojo}/mojoshader.c" "${wa2_mojo}/mojoshader_common.c" ${wa2_mojo_profiles})
target_include_directories(wa2_common_shader_parser PUBLIC "${wa2_mojo}")
target_compile_definitions(wa2_common_shader_parser PUBLIC MOJOSHADER_NO_VERSION_INCLUDE PRIVATE
    SUPPORT_PROFILE_D3D=0 SUPPORT_PROFILE_HLSL=0 SUPPORT_PROFILE_ARB1=0 SUPPORT_PROFILE_ARB1_NV=0
    SUPPORT_PROFILE_METAL=0 SUPPORT_PROFILE_SPIRV=0 SUPPORT_PROFILE_GLSPIRV=0
    MOJOSHADER_FLIP_RENDERTARGET MOJOSHADER_DEPTH_CLIPPING)
set_target_properties(wa2_common_shader_parser PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(wa2_common_shader_gl STATIC "${wa2_mojo}/mojoshader_opengl.c")
target_include_directories(wa2_common_shader_gl PUBLIC "${wa2_mojo}")
target_compile_definitions(wa2_common_shader_gl PUBLIC MOJOSHADER_NO_VERSION_INCLUDE PRIVATE
    SUPPORT_PROFILE_D3D=0 SUPPORT_PROFILE_HLSL=0 SUPPORT_PROFILE_ARB1=0 SUPPORT_PROFILE_ARB1_NV=0
    SUPPORT_PROFILE_METAL=0 SUPPORT_PROFILE_SPIRV=0 SUPPORT_PROFILE_GLSPIRV=0
    MOJOSHADER_FLIP_RENDERTARGET MOJOSHADER_DEPTH_CLIPPING)
target_link_libraries(wa2_common_shader_gl PUBLIC wa2_common_shader_parser)
set_target_properties(wa2_common_shader_gl PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(wa2_common_shader_assembler STATIC
    "${wa2_assembler}/mojoshader_assembler_common.c" "${wa2_assembler}/mojoshader_preprocessor.c"
    "${wa2_assembler}/mojoshader_lexer.c" "${wa2_assembler}/mojoshader_assembler.c")
target_include_directories(wa2_common_shader_assembler PUBLIC "${wa2_assembler}")
target_compile_definitions(wa2_common_shader_assembler PUBLIC MOJOSHADER_ASSEMBLER_NO_VERSION_INCLUDE)
target_link_libraries(wa2_common_shader_assembler PUBLIC wa2_common_shader_parser)
set_target_properties(wa2_common_shader_assembler PROPERTIES POSITION_INDEPENDENT_CODE ON)
foreach(symbol MOJOSHADER_internal_malloc MOJOSHADER_internal_free
    stringcache stringcache_len stringcache_iscached stringcache_fmt stringcache_create stringcache_destroy
    errorlist_create errorlist_add errorlist_add_fmt errorlist_add_va errorlist_count errorlist_flatten errorlist_destroy
    buffer_create buffer_reserve buffer_append buffer_append_fmt buffer_append_va buffer_size buffer_flatten buffer_empty buffer_destroy buffer_merge buffer_find buffer_patch
    MOJOSHADER_printFloat MOJOSHADER_out_of_mem_error MOJOSHADER_out_of_mem_data)
    target_compile_definitions(wa2_common_shader_assembler PRIVATE "${symbol}=WA2_assembler_${symbol}")
endforeach()
if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    target_compile_options(wa2_common_shader_assembler PRIVATE -Wno-int-conversion)
endif()
add_library(wa2_fixed_pipeline STATIC "${CMAKE_CURRENT_LIST_DIR}/fixed_pipeline.cpp" "${CMAKE_CURRENT_LIST_DIR}/shader_source.cpp")
target_compile_features(wa2_fixed_pipeline PUBLIC cxx_std_20)
target_include_directories(wa2_fixed_pipeline PUBLIC "${CMAKE_CURRENT_LIST_DIR}")
set_target_properties(wa2_fixed_pipeline PROPERTIES POSITION_INDEPENDENT_CODE ON)
