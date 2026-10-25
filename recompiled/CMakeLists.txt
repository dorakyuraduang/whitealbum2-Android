cmake_minimum_required(VERSION 3.20)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(WA2SharedAot LANGUAGES C CXX)
endif()
get_filename_component(WA2_SHARED_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
file(READ "${WA2_SHARED_ROOT}/config/porting_contract.json" WA2_PORTING_CONTRACT)
string(JSON WA2_SHARED_CODE_REQUIRED GET "${WA2_PORTING_CONTRACT}" rules one_aot_source_set_for_all_host_architectures)
if(NOT WA2_SHARED_CODE_REQUIRED)
    message(FATAL_ERROR "All host architectures must use one shared AOT source set")
endif()
include("${WA2_SHARED_ROOT}/generated/shared-aot/sources.cmake")
add_library(wa2_initial_data STATIC "${WA2_SHARED_ROOT}/generated/static-data/image.cpp")
target_include_directories(wa2_initial_data PUBLIC "${WA2_SHARED_ROOT}/tools/WindowsRecompTool-main/include")
target_compile_features(wa2_initial_data PUBLIC cxx_std_20)
set_target_properties(wa2_initial_data PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(wa2_shared_aot STATIC functions.cpp extended80.cpp transcend.cpp native_gateways.cpp native_math.cpp native_string.cpp image_colours.cpp uv_clip.cpp vertex_grid.cpp native_weather.cpp native_sprites.cpp
    "${WA2_SHARED_ROOT}/port/sprite_sort.cpp"
    "${WA2_SHARED_ROOT}/port/vertex_batch.cpp" ${WA2_SHARED_AOT_SOURCES})
target_link_libraries(wa2_shared_aot PUBLIC wa2_initial_data)
target_compile_features(wa2_shared_aot PUBLIC cxx_std_20)
target_include_directories(wa2_shared_aot PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}"
    "${WA2_SHARED_ROOT}/tools/WindowsRecompTool-main/include")
set_target_properties(wa2_shared_aot PROPERTIES POSITION_INDEPENDENT_CODE ON)
# The same memory implementation selects OS allocation primitives. No decoder,
# GUI, Windows imports, or architecture-specific assembly is linked here.
add_library(wa2_aot_memory STATIC
    "${WA2_SHARED_ROOT}/tools/WindowsRecompTool-main/src/runtime/native_memory.cpp")
target_include_directories(wa2_aot_memory PUBLIC "${WA2_SHARED_ROOT}/tools/WindowsRecompTool-main/include")
target_compile_features(wa2_aot_memory PUBLIC cxx_std_20)
set_target_properties(wa2_aot_memory PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(wa2_shared_aot PUBLIC wa2_aot_memory)
set(WA2_FUNCTION_RUNTIME_SOURCES
    "${WA2_SHARED_ROOT}/tools/WindowsRecompTool-main/src/pe.cpp"
    "${WA2_SHARED_ROOT}/tools/WindowsRecompTool-main/src/resources.cpp"
    "${WA2_SHARED_ROOT}/tools/WindowsRecompTool-main/src/runtime/process.cpp"
    "${WA2_SHARED_ROOT}/tools/WindowsRecompTool-main/src/runtime/win32.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/process.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/subtitles.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/movie_settings.cpp"
    "${WA2_SHARED_ROOT}/port/native_resources.cpp"
    "${WA2_SHARED_ROOT}/port/native_vorbis.cpp"
    "${WA2_SHARED_ROOT}/port/registry.cpp"
    "${WA2_SHARED_ROOT}/port/exceptions.cpp"
    "${WA2_SHARED_ROOT}/platform/compat/profile.cpp"
    "${WA2_SHARED_ROOT}/platform/compat/system.cpp"
    "${WA2_SHARED_ROOT}/platform/compat/codepage.cpp"
    "${WA2_SHARED_ROOT}/platform/compat/text.cpp"
    "${WA2_SHARED_ROOT}/platform/compat/locale.cpp"
    "${WA2_SHARED_ROOT}/platform/compat/resource_mount.cpp"
    "${WA2_SHARED_ROOT}/platform/compat/test_profile.cpp"
    "${WA2_SHARED_ROOT}/platform/compat/legacy_display.cpp"
    "${WA2_SHARED_ROOT}/platform/compat/media.cpp"
    "${WA2_SHARED_ROOT}/platform/compat/acm.cpp"
    "${WA2_SHARED_ROOT}/platform/compat/shell.cpp"
    "${WA2_SHARED_ROOT}/platform/window/resource_templates.cpp"
    "${WA2_SHARED_ROOT}/platform/window/gui.cpp"
    "${WA2_SHARED_ROOT}/platform/window/sdl_host.cpp"
    "${WA2_SHARED_ROOT}/generated/codepages/tables.cpp"
    "${WA2_SHARED_ROOT}/platform/compat/scheduler.cpp")
if(NOT TARGET wa2_engine)
    add_subdirectory("${WA2_SHARED_ROOT}/engine" native_engine EXCLUDE_FROM_ALL)
endif()
if(NOT TARGET wa2_platform)
    include("${WA2_SHARED_ROOT}/platform/portable.cmake")
endif()
target_link_libraries(wa2_shared_aot PUBLIC wa2_scalar_math)
if(NOT TARGET wa2_movie)
    include("${WA2_SHARED_ROOT}/engine/movie.cmake")
endif()
set_target_properties(wa2_engine wa2_platform PROPERTIES POSITION_INDEPENDENT_CODE ON)
include("${WA2_SHARED_ROOT}/platform/compat/audio.cmake")
enable_language(C)
include("${WA2_SHARED_ROOT}/platform/render/renderer.cmake")
include("${WA2_SHARED_ROOT}/platform/window/widgets.cmake")
add_library(wa2_function_process STATIC ${WA2_FUNCTION_RUNTIME_SOURCES})
target_compile_features(wa2_function_process PUBLIC cxx_std_20)
target_compile_definitions(wa2_function_process PUBLIC WA2_STATIC_FUNCTIONS_ONLY=1)
target_link_libraries(wa2_function_process PUBLIC wa2_shared_aot wa2_aot_memory wa2_platform wa2_engine wa2_shared_audio wa2_shared_graphics wa2_native_widgets wa2_movie wa2_wave_codec)
set_target_properties(wa2_function_process PROPERTIES POSITION_INDEPENDENT_CODE ON)
option(WA2_SHARED_AOT_CHECKS "Build the memory-only differential check" OFF)
option(WA2_AOT_DIAGNOSTICS "Include instrumented AOT bodies for differential tests" ${WA2_SHARED_AOT_CHECKS})
target_compile_definitions(wa2_shared_aot PUBLIC WA2_AOT_DIAGNOSTICS=$<BOOL:${WA2_AOT_DIAGNOSTICS}>)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(wa2_shared_aot PRIVATE -Oz)
    # Measured CPU hotspots keep the same checked source bodies on both hosts.
    # Isolating them avoids expanding the whole static program for inlining.
    set_source_files_properties("${WA2_SHARED_ROOT}/generated/shared-aot/functions_hot.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/extended80.cpp" "${CMAKE_CURRENT_LIST_DIR}/native_string.cpp" "${CMAKE_CURRENT_LIST_DIR}/uv_clip.cpp" "${CMAKE_CURRENT_LIST_DIR}/vertex_grid.cpp" "${CMAKE_CURRENT_LIST_DIR}/native_weather.cpp" PROPERTIES COMPILE_OPTIONS "-O2")
    set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/functions.cpp"
        "${WA2_SHARED_ROOT}/tools/WindowsRecompTool-main/src/runtime/process.cpp"
        "${WA2_SHARED_ROOT}/platform/compat/system.cpp" PROPERTIES COMPILE_OPTIONS "-O2")
    set_property(SOURCE "${CMAKE_CURRENT_LIST_DIR}/vertex_grid.cpp" APPEND PROPERTY COMPILE_OPTIONS "-ffp-contract=off;-frounding-math")
    set_property(SOURCE "${CMAKE_CURRENT_LIST_DIR}/native_weather.cpp" APPEND PROPERTY COMPILE_OPTIONS "-ffp-contract=off;-frounding-math")
    set_property(SOURCE "${CMAKE_CURRENT_LIST_DIR}/native_sprites.cpp" APPEND PROPERTY COMPILE_OPTIONS "-O2;-ffp-contract=off;-frounding-math")
    # Keep the bounded word-fill loop vectorized. Some size-oriented bundled
    # C runtimes implement large memset with a byte loop.
    set_property(SOURCE "${CMAKE_CURRENT_LIST_DIR}/native_string.cpp" APPEND PROPERTY COMPILE_OPTIONS "-fno-builtin-memset")
    set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/image_colours.cpp" PROPERTIES COMPILE_OPTIONS "-O3")
    # Resource admission/snapshots run on every new image/script load. Keep
    # these native loops optimized on both hosts without expanding AOT bodies.
    set_source_files_properties("${WA2_SHARED_ROOT}/port/native_resources.cpp" PROPERTIES COMPILE_OPTIONS "-O3")
    # LLVM's COFF outliner currently emits invalid SEH frame directives here.
    # This is a host object-format option; both hosts compile the same bodies.
    if(NOT WIN32)
        target_compile_options(wa2_shared_aot PRIVATE -mllvm -enable-machine-outliner=always)
    endif()
endif()
if(WA2_SHARED_AOT_CHECKS)
    if(NOT WA2_AOT_DIAGNOSTICS)
        message(FATAL_ERROR "Differential checks require the diagnostic build")
    endif()
    add_executable(wa2_shared_aot_tests
        "${WA2_SHARED_ROOT}/tests/shared_aot.cpp"
        ${WA2_AOT_ORACLE_SOURCES})
    # The oracle is deliberately a large instruction switch. Optimizing that
    # test-only switch is expensive and cannot improve the shipped program.
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        set_source_files_properties(${WA2_AOT_ORACLE_SOURCES}
            PROPERTIES COMPILE_OPTIONS "-O0;-fno-sanitize=all")
    endif()
    target_link_libraries(wa2_shared_aot_tests PRIVATE wa2_shared_aot wa2_aot_memory)
    add_library(wa2_aot_fp_oracle STATIC IMPORTED)
    set_target_properties(wa2_aot_fp_oracle PROPERTIES IMPORTED_LOCATION "${WA2_SHARED_ROOT}/build/wa2-win64/winrecomp/libwinrecomp_native.a")
    target_link_libraries(wa2_shared_aot_tests PRIVATE wa2_aot_fp_oracle)
    add_executable(wa2_function_process_tests "${WA2_SHARED_ROOT}/tests/function_process.cpp")
    target_link_libraries(wa2_function_process_tests PRIVATE wa2_function_process)
    add_executable(wa2_subtitle_tests "${WA2_SHARED_ROOT}/tests/subtitles.cpp")
    target_link_libraries(wa2_subtitle_tests PRIVATE wa2_function_process)
    add_executable(wa2_single_tile_geometry_tests "${WA2_SHARED_ROOT}/tests/single_tile_geometry.cpp")
    target_compile_features(wa2_single_tile_geometry_tests PRIVATE cxx_std_20)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(wa2_single_tile_geometry_tests PRIVATE -O2 -ffp-contract=off -frounding-math)
    endif()
    add_executable(wa2_portable_scheduler_tests "${WA2_SHARED_ROOT}/tests/portable_scheduler.cpp")
    target_link_libraries(wa2_portable_scheduler_tests PRIVATE wa2_function_process)
    add_executable(wa2_portable_registry_tests "${WA2_SHARED_ROOT}/tests/portable_registry.cpp")
    target_link_libraries(wa2_portable_registry_tests PRIVATE wa2_function_process)
    add_executable(wa2_portable_system_tests "${WA2_SHARED_ROOT}/tests/portable_system.cpp")
    target_link_libraries(wa2_portable_system_tests PRIVATE wa2_function_process)
    add_executable(wa2_resource_mount_tests "${WA2_SHARED_ROOT}/tests/resource_mount.cpp")
    target_link_libraries(wa2_resource_mount_tests PRIVATE wa2_function_process)
    add_executable(wa2_test_profile_tests "${WA2_SHARED_ROOT}/tests/test_profile.cpp")
    target_link_libraries(wa2_test_profile_tests PRIVATE wa2_function_process)
    add_executable(wa2_script_wait_callback_tests "${WA2_SHARED_ROOT}/tests/script_wait_callback.cpp")
    target_link_libraries(wa2_script_wait_callback_tests PRIVATE wa2_function_process)
    add_executable(wa2_native_resource_memory_tests "${WA2_SHARED_ROOT}/tests/native_resource_memory.cpp")
    target_link_libraries(wa2_native_resource_memory_tests PRIVATE wa2_function_process)
    add_executable(wa2_native_resource_cache_tests "${WA2_SHARED_ROOT}/tests/native_resource_cache.cpp")
    target_link_libraries(wa2_native_resource_cache_tests PRIVATE wa2_function_process)
    add_executable(wa2_resource_loading_tests "${WA2_SHARED_ROOT}/tests/resource_loading.cpp")
    target_link_libraries(wa2_resource_loading_tests PRIVATE wa2_function_process)
    add_executable(wa2_lzss_group_tests "${WA2_SHARED_ROOT}/tests/lzss_groups.cpp")
    target_link_libraries(wa2_lzss_group_tests PRIVATE wa2_engine)
    add_executable(wa2_glyph_index_tests "${WA2_SHARED_ROOT}/tests/glyph_index.cpp")
    target_compile_features(wa2_glyph_index_tests PRIVATE cxx_std_20)
    add_executable(wa2_glyph_geometry_tests "${WA2_SHARED_ROOT}/tests/glyph_geometry.cpp")
    target_compile_features(wa2_glyph_geometry_tests PRIVATE cxx_std_20)
    add_executable(wa2_byte_equality_tests "${WA2_SHARED_ROOT}/tests/byte_equality.cpp")
    target_compile_features(wa2_byte_equality_tests PRIVATE cxx_std_20)
    add_executable(wa2_wait_probe_tests "${WA2_SHARED_ROOT}/tests/wait_probes.cpp")
    target_link_libraries(wa2_wait_probe_tests PRIVATE wa2_function_process)
    add_executable(wa2_heap_pool_memory_tests "${WA2_SHARED_ROOT}/tests/heap_pool_memory.cpp")
    target_link_libraries(wa2_heap_pool_memory_tests PRIVATE wa2_function_process)
    add_executable(wa2_large_heap_memory_tests "${WA2_SHARED_ROOT}/tests/large_heap_memory.cpp")
    target_link_libraries(wa2_large_heap_memory_tests PRIVATE wa2_function_process)
    add_executable(wa2_portable_sound_bridge_tests "${WA2_SHARED_ROOT}/tests/portable_sound_bridge.cpp")
    target_link_libraries(wa2_portable_sound_bridge_tests PRIVATE wa2_function_process)
    add_executable(wa2_window_resource_tests "${WA2_SHARED_ROOT}/tests/window_resources.cpp")
    target_link_libraries(wa2_window_resource_tests PRIVATE wa2_function_process)
    add_executable(wa2_widget_unicode_tests "${WA2_SHARED_ROOT}/tests/widget_unicode.cpp")
    target_link_libraries(wa2_widget_unicode_tests PRIVATE wa2_function_process)
    add_executable(wa2_window_model_tests "${WA2_SHARED_ROOT}/tests/window_model.cpp")
    target_link_libraries(wa2_window_model_tests PRIVATE wa2_function_process)
    add_executable(wa2_portable_movie_bridge_tests "${WA2_SHARED_ROOT}/tests/portable_movie_bridge.cpp")
    target_link_libraries(wa2_portable_movie_bridge_tests PRIVATE wa2_function_process)
    add_executable(wa2_movie_track_tests "${WA2_SHARED_ROOT}/tests/movie_tracks.cpp")
    target_link_libraries(wa2_movie_track_tests PRIVATE wa2_movie wa2_platform)
    add_executable(wa2_render_submission_tests "${WA2_SHARED_ROOT}/tests/render_submission.cpp")
    target_compile_features(wa2_render_submission_tests PRIVATE cxx_std_20)
    target_link_libraries(wa2_render_submission_tests PRIVATE wa2_fixed_pipeline)
    add_executable(wa2_portable_acm_tests "${WA2_SHARED_ROOT}/tests/portable_acm.cpp")
    target_link_libraries(wa2_portable_acm_tests PRIVATE wa2_function_process)
    add_executable(wa2_portable_shell_tests "${WA2_SHARED_ROOT}/tests/portable_shell.cpp")
    target_link_libraries(wa2_portable_shell_tests PRIVATE wa2_function_process)
    add_executable(wa2_crt_math_path_tests "${WA2_SHARED_ROOT}/tests/crt_math_paths.cpp")
    target_link_libraries(wa2_crt_math_path_tests PRIVATE wa2_function_process wa2_aot_fp_oracle)
    add_executable(wa2_render_cpu_tests "${WA2_SHARED_ROOT}/tests/render_cpu.cpp")
    target_link_libraries(wa2_render_cpu_tests PRIVATE wa2_function_process)
    add_executable(wa2_dialogue_cpu_tests "${WA2_SHARED_ROOT}/tests/dialogue_cpu.cpp")
    target_link_libraries(wa2_dialogue_cpu_tests PRIVATE wa2_function_process)
    add_executable(wa2_native_sprite_cpu_tests "${WA2_SHARED_ROOT}/tests/native_sprite_cpu.cpp")
    target_link_libraries(wa2_native_sprite_cpu_tests PRIVATE wa2_function_process)
    add_executable(wa2_particle_pipeline_tests "${WA2_SHARED_ROOT}/tests/particle_pipeline.cpp")
    target_link_libraries(wa2_particle_pipeline_tests PRIVATE wa2_function_process)
    add_executable(wa2_weather_cpu_tests "${WA2_SHARED_ROOT}/tests/weather_cpu.cpp")
    target_compile_definitions(wa2_weather_cpu_tests PRIVATE WA2_PARTICLE_WORK_DIAGNOSTICS=1)
    target_link_libraries(wa2_weather_cpu_tests PRIVATE wa2_function_process)
    add_executable(wa2_interface_handle_tests "${WA2_SHARED_ROOT}/tests/interface_handles.cpp")
    target_link_libraries(wa2_interface_handle_tests PRIVATE wa2_function_process)
    add_executable(wa2_audio_latency_tests "${WA2_SHARED_ROOT}/tests/audio_latency.cpp")
    target_include_directories(wa2_audio_latency_tests PRIVATE "${WA2_SHARED_ROOT}/tools/WindowsRecompTool-main/include")
    target_link_libraries(wa2_audio_latency_tests PRIVATE wa2_engine wa2_platform)
    add_executable(wa2_audio_latency_before "${WA2_SHARED_ROOT}/tests/audio_latency.cpp"
        "${WA2_SHARED_ROOT}/build/performance-0.2.19/before-pcm_audio.cpp"
        "${WA2_SHARED_ROOT}/build/performance-0.2.19/before-vorbis_stream.cpp")
    target_compile_options(wa2_audio_latency_before PRIVATE -Oz -ffp-contract=off)
    target_include_directories(wa2_audio_latency_before PRIVATE "${WA2_SHARED_ROOT}/tools/WindowsRecompTool-main/include")
    target_link_libraries(wa2_audio_latency_before PRIVATE wa2_engine wa2_platform)
    add_executable(wa2_host_leaf_tests "${WA2_SHARED_ROOT}/tests/host_leaf_calls.cpp")
    target_link_libraries(wa2_host_leaf_tests PRIVATE wa2_function_process)
    add_executable(wa2_integer_register_tests "${WA2_SHARED_ROOT}/tests/integer_registers.cpp")
    target_link_libraries(wa2_integer_register_tests PRIVATE wa2_shared_aot)
    add_executable(wa2_native_string_tests "${WA2_SHARED_ROOT}/tests/native_string.cpp")
    target_link_libraries(wa2_native_string_tests PRIVATE wa2_shared_aot)
    add_executable(wa2_native_memory_tests "${WA2_SHARED_ROOT}/tests/native_memory.cpp")
    target_link_libraries(wa2_native_memory_tests PRIVATE wa2_shared_aot)
    add_executable(wa2_image_colour_tests "${WA2_SHARED_ROOT}/tests/image_colours.cpp")
    target_link_libraries(wa2_image_colour_tests PRIVATE wa2_shared_aot)
    add_executable(wa2_pixel_transfer_tests "${WA2_SHARED_ROOT}/tests/pixel_transfer.cpp")
    add_executable(wa2_image_upload_tests "${WA2_SHARED_ROOT}/tests/image_upload.cpp" "${WA2_SHARED_ROOT}/tests/image_upload_before.cpp")
    target_link_libraries(wa2_image_upload_tests PRIVATE wa2_pixel_transfer wa2_aot_memory)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        set_source_files_properties("${WA2_SHARED_ROOT}/tests/image_upload_before.cpp" PROPERTIES COMPILE_OPTIONS "-O2")
    endif()
    target_link_libraries(wa2_pixel_transfer_tests PRIVATE wa2_pixel_transfer wa2_aot_memory)
endif()
