include("${CMAKE_CURRENT_SOURCE_DIR}/sdl_backend.cmake")
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../engine" engine)
include("${CMAKE_CURRENT_SOURCE_DIR}/../platform/portable.cmake")
add_library(wa2_sdl_audio STATIC "${CMAKE_CURRENT_SOURCE_DIR}/../platform/sdl_audio.cpp")
target_include_directories(wa2_sdl_audio PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../platform")
target_link_libraries(wa2_sdl_audio PUBLIC wa2_engine PRIVATE SDL2-static)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
get_filename_component(WA2_PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
get_filename_component(WA2_IMAGE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../WA2_chs.exe" ABSOLUTE)
file(SHA256 "${WA2_IMAGE_PATH}" WA2_IMAGE_SHA256)
if(NOT WA2_IMAGE_SHA256 STREQUAL "246a263b965147a274fa59cdd7a72f6be10ba063d8cf4e61e74baefb56f29327")
    message(FATAL_ERROR "Embedded image does not match the statically translated program")
endif()
# Keep the batched statements tied to the current single-instruction oracle.
# These are file-producing rules, so an unchanged build does not run the
# generators or rewrite their outputs. Missing outputs trigger regeneration.
set(WA2_STATIC_BLOCK_ORACLES)
foreach(page IN ITEMS 1032 1081 1083 1084 1085 1110 1124 1129 1134 1136 1139 1140 1150 1183)
    list(APPEND WA2_STATIC_BLOCK_ORACLES "${WA2_PROJECT_ROOT}/generated/wa2-win64/code_page_${page}.cpp")
endforeach()
set(WA2_STATIC_BLOCK_OUTPUTS
    "${CMAKE_CURRENT_SOURCE_DIR}/static_blocks_408.inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/static_blocks_439.inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/static_blocks_43b.inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/static_blocks_43c.inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/static_blocks_43d.inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/static_blocks_456.inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/static_blocks_464.inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/static_blocks_469.inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/static_blocks_46e.inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/static_blocks_470.inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/static_blocks_473.inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/static_blocks_474.inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/static_blocks_47e.inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/static_blocks_49f.inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/static_blocks_entries.inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/static_blocks_manifest.json")
add_custom_command(
    OUTPUT ${WA2_STATIC_BLOCK_OUTPUTS}
    COMMAND "${Python3_EXECUTABLE}" "${WA2_PROJECT_ROOT}/scripts/generate_static_blocks.py"
    DEPENDS "${WA2_PROJECT_ROOT}/scripts/generate_static_blocks.py"
            "${WA2_PROJECT_ROOT}/logs/cfg.json" "${WA2_IMAGE_PATH}" "${CMAKE_CURRENT_SOURCE_DIR}/specialized_entries.inc"
            "${CMAKE_CURRENT_SOURCE_DIR}/fp_sequences_manifest.json" ${WA2_STATIC_BLOCK_ORACLES}
            "${CMAKE_CURRENT_SOURCE_DIR}/native_backend_manifest.json"
    WORKING_DIRECTORY "${WA2_PROJECT_ROOT}"
    COMMENT "Regenerating static integer blocks from the current oracle"
    VERBATIM)
set(WA2_VORBIS_BATCH_OUTPUTS
    "${CMAKE_CURRENT_SOURCE_DIR}/vorbis_batch.S"
    "${CMAKE_CURRENT_SOURCE_DIR}/vorbis_signature.inc")
add_custom_command(
    OUTPUT ${WA2_VORBIS_BATCH_OUTPUTS}
    COMMAND "${Python3_EXECUTABLE}" "${WA2_PROJECT_ROOT}/scripts/generate_vorbis_batch.py"
    DEPENDS "${WA2_PROJECT_ROOT}/scripts/generate_vorbis_batch.py" "${WA2_IMAGE_PATH}"
    WORKING_DIRECTORY "${WA2_PROJECT_ROOT}"
    COMMENT "Regenerating the SHA-bound native Vorbis batch"
    VERBATIM)
set(WA2_VORBIS_BUTTERFLY_OUTPUTS
    "${CMAKE_CURRENT_SOURCE_DIR}/vorbis_butterfly.S"
    "${CMAKE_CURRENT_SOURCE_DIR}/vorbis_butterfly_signature.inc")
add_custom_command(
    OUTPUT ${WA2_VORBIS_BUTTERFLY_OUTPUTS}
    COMMAND "${Python3_EXECUTABLE}" "${WA2_PROJECT_ROOT}/scripts/generate_vorbis_butterfly.py"
    DEPENDS "${WA2_PROJECT_ROOT}/scripts/generate_vorbis_butterfly.py" "${WA2_IMAGE_PATH}"
    WORKING_DIRECTORY "${WA2_PROJECT_ROOT}"
    COMMENT "Regenerating the SHA-bound native Vorbis butterfly"
    VERBATIM)
set(WA2_UV_BATCH_OUTPUTS
    "${CMAKE_CURRENT_SOURCE_DIR}/uv_batch.S"
    "${CMAKE_CURRENT_SOURCE_DIR}/uv_signature.inc")
add_custom_command(
    OUTPUT ${WA2_UV_BATCH_OUTPUTS}
    COMMAND "${Python3_EXECUTABLE}" "${WA2_PROJECT_ROOT}/scripts/generate_uv_batch.py"
    DEPENDS "${WA2_PROJECT_ROOT}/scripts/generate_uv_batch.py" "${WA2_IMAGE_PATH}"
    WORKING_DIRECTORY "${WA2_PROJECT_ROOT}"
    COMMENT "Regenerating the SHA-bound native UV loops"
    VERBATIM)
set(WA2_FP_ORACLES)
foreach(page IN ITEMS 1032 1082 1083 1084 1085 1110 1129 1133 1134 1136 1139 1140)
    list(APPEND WA2_FP_ORACLES "${WA2_PROJECT_ROOT}/generated/wa2-win64/code_page_${page}.cpp")
endforeach()
set(WA2_FP_SEQUENCE_OUTPUTS
    "${CMAKE_CURRENT_SOURCE_DIR}/fp_sequences.S"
    "${CMAKE_CURRENT_SOURCE_DIR}/fp_sequences_data.inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/fp_sequences_manifest.json")
add_custom_command(
    OUTPUT ${WA2_FP_SEQUENCE_OUTPUTS}
    COMMAND "${Python3_EXECUTABLE}" "${WA2_PROJECT_ROOT}/scripts/generate_fp_sequences.py"
    DEPENDS "${WA2_PROJECT_ROOT}/scripts/generate_fp_sequences.py" "${WA2_IMAGE_PATH}" "${CMAKE_CURRENT_SOURCE_DIR}/specialized_entries.inc" ${WA2_FP_ORACLES}
        "${WA2_PROJECT_ROOT}/scripts/retired_modules.py" "${CMAKE_CURRENT_SOURCE_DIR}/retired_modules.json" "${CMAKE_CURRENT_SOURCE_DIR}/native_functions.json"
    WORKING_DIRECTORY "${WA2_PROJECT_ROOT}"
    COMMENT "Regenerating native FP sequences from the current oracle"
    VERBATIM)
add_custom_target(wa2_generated_batches DEPENDS ${WA2_STATIC_BLOCK_OUTPUTS} ${WA2_VORBIS_BATCH_OUTPUTS}
    ${WA2_VORBIS_BUTTERFLY_OUTPUTS} ${WA2_UV_BATCH_OUTPUTS} ${WA2_FP_SEQUENCE_OUTPUTS})
set(WA2_NATIVE_OUTPUTS "${CMAKE_CURRENT_SOURCE_DIR}/native_backend.S"
    "${CMAKE_CURRENT_SOURCE_DIR}/native_backend_data.inc" "${CMAKE_CURRENT_SOURCE_DIR}/native_backend_manifest.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/native_backend_test_data.inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/native_linear.S" "${CMAKE_CURRENT_SOURCE_DIR}/native_linear_manifest.json")
file(GLOB WA2_NATIVE_ORACLES CONFIGURE_DEPENDS "${WA2_PROJECT_ROOT}/generated/wa2-win64/code_page_*.cpp")
add_custom_command(OUTPUT ${WA2_NATIVE_OUTPUTS}
    COMMAND "${Python3_EXECUTABLE}" "${WA2_PROJECT_ROOT}/scripts/generate_native_backend_v2.py"
    DEPENDS "${WA2_PROJECT_ROOT}/scripts/generate_native_backend_v2.py"
        "${WA2_PROJECT_ROOT}/scripts/generate_static_blocks.py" "${WA2_PROJECT_ROOT}/logs/cfg.json"
        "${WA2_PROJECT_ROOT}/scripts/retired_modules.py" "${CMAKE_CURRENT_SOURCE_DIR}/retired_modules.json"
        "${WA2_IMAGE_PATH}" "${CMAKE_CURRENT_SOURCE_DIR}/specialized_entries.inc" "${CMAKE_CURRENT_SOURCE_DIR}/native_functions.json" ${WA2_NATIVE_ORACLES}
    WORKING_DIRECTORY "${WA2_PROJECT_ROOT}" VERBATIM)
add_custom_target(wa2_generated_native DEPENDS ${WA2_NATIVE_OUTPUTS})
add_custom_target(wa2_native_generation_checked
    COMMAND "${Python3_EXECUTABLE}" "${WA2_PROJECT_ROOT}/scripts/verify_native_generation.py"
    DEPENDS wa2_generated_native
    WORKING_DIRECTORY "${WA2_PROJECT_ROOT}" VERBATIM)
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/sprite_transform.S" "${CMAKE_CURRENT_SOURCE_DIR}/sprite_transform_signature.inc"
    COMMAND "${Python3_EXECUTABLE}" "${WA2_PROJECT_ROOT}/scripts/generate_sprite_transform.py"
    DEPENDS "${WA2_PROJECT_ROOT}/scripts/generate_sprite_transform.py" "${WA2_IMAGE_PATH}"
    WORKING_DIRECTORY "${WA2_PROJECT_ROOT}" VERBATIM)
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/bitreader_signature.inc"
    COMMAND "${Python3_EXECUTABLE}" "${WA2_PROJECT_ROOT}/scripts/generate_bitreader_batch.py"
    DEPENDS "${WA2_PROJECT_ROOT}/scripts/generate_bitreader_batch.py" "${WA2_IMAGE_PATH}"
    WORKING_DIRECTORY "${WA2_PROJECT_ROOT}" VERBATIM)
add_custom_target(wa2_generated_sprite_bitreader DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/sprite_transform.S" "${CMAKE_CURRENT_SOURCE_DIR}/sprite_transform_signature.inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/bitreader_signature.inc")
configure_file(embedded_image.S.in "${CMAKE_CURRENT_BINARY_DIR}/embedded_image.S" @ONLY)
set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/embedded_image.S" PROPERTIES OBJECT_DEPENDS "${WA2_IMAGE_PATH}")
target_sources(recompiled_program PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/embedded_image.S")
set_target_properties(recompiled_program PROPERTIES WIN32_EXECUTABLE TRUE)
target_link_options(recompiled_program PRIVATE "-Wl,--subsystem,windows")
# Zig 0.15.2's COFF driver emits -DEBUG without -OPT:REF. Its GNU-style
# --gc-sections is not forwarded to LLD's COFF path. Retain the matching PDB
# and explicitly collect unreachable COMDATs in the release linker launcher.
if(CMAKE_CXX_COMPILER MATCHES "zig([.]exe)?$")
    set_property(TARGET recompiled_program PROPERTY CXX_LINKER_LAUNCHER
        "${Python3_EXECUTABLE};${WA2_PROJECT_ROOT}/scripts/link_release_pe.py")
    set_property(TARGET recompiled_program APPEND PROPERTY LINK_DEPENDS
        "${WA2_PROJECT_ROOT}/scripts/link_release_pe.py")
endif()
add_library(wa2_port STATIC wa2_win32.cpp format.cpp shell.cpp kernel.cpp kernel64.cpp file_dialogs.cpp exceptions.cpp registry.cpp direct3d_sdl.cpp audio.cpp acm.cpp media.cpp threads.cpp fast_paths.cpp color_batch.cpp vertex_batch.cpp)
add_executable(wa2_existing_sort_tests "${WA2_PROJECT_ROOT}/tests/existing_sort.cpp"
    "${WA2_PROJECT_ROOT}/generated/wa2-win64/code_page_1090.cpp")
target_link_libraries(wa2_existing_sort_tests PRIVATE wa2_port)
target_sources(wa2_port PRIVATE native_resources.cpp "${CMAKE_CURRENT_LIST_DIR}/../platform/compat/resource_mount.cpp")
target_sources(wa2_port PRIVATE sprite_sort.cpp)
target_link_libraries(wa2_port PUBLIC wa2_platform)
add_dependencies(wa2_port wa2_generated_batches)
add_dependencies(wa2_port wa2_generated_sprite_bitreader)
add_dependencies(wa2_port wa2_generated_native)
add_dependencies(wa2_port wa2_native_generation_checked)
target_link_libraries(wa2_port PUBLIC winrecomp_guest wa2_sdl_audio winmm ole32 oleaut32 msacm32 strmiids comdlg32 shell32)
target_link_libraries(wa2_port PRIVATE SDL2-static wa2_mojoshader wa2_shader_assembler opengl32 dxguid)
target_include_directories(wa2_port PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
option(WA2_COMPACT_FALLBACK "Share identical C++ fallback function bodies" ON)
option(WA2_CPP_BLOCK_BACKEND "Include the duplicate C++ multi-instruction backend" OFF)
if(WA2_CPP_BLOCK_BACKEND OR NOT WA2_COMPACT_FALLBACK)
    target_sources(wa2_port PRIVATE static_blocks.cpp)
endif()
if(WA2_CPP_BLOCK_BACKEND)
    target_compile_definitions(wa2_port PRIVATE WA2_CPP_BLOCK_BACKEND=1)
endif()
add_library(wa2_block_reference STATIC static_blocks.cpp)
target_link_libraries(wa2_block_reference PUBLIC winrecomp_guest)
target_include_directories(wa2_block_reference PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
add_dependencies(wa2_block_reference wa2_generated_batches wa2_generated_native)
option(WA2_PAGED_NATIVE_BACKEND "Include the duplicate descriptor native backend for development" OFF)
option(WA2_NATIVE_PROFILE_POINTS "Embed the large host-PC sampling table for development" OFF)
target_sources(wa2_port PRIVATE native_backend.cpp native_backend_metadata.cpp native_linear.S linear_faults.cpp)
target_sources(wa2_port PRIVATE native_vorbis.cpp)
add_executable(wa2_native_vorbis_tests "${WA2_PROJECT_ROOT}/tests/vorbis_stream.cpp"
    "${WA2_PROJECT_ROOT}/generated/wa2-win64/dispatch.cpp" ${WA2_NATIVE_ORACLES})
target_link_libraries(wa2_native_vorbis_tests PRIVATE wa2_port)
if(WA2_PAGED_NATIVE_BACKEND)
    target_sources(wa2_port PRIVATE native_backend.S)
    target_compile_definitions(wa2_port PRIVATE WA2_PAGED_NATIVE_BACKEND=1)
endif()
if(WA2_NATIVE_PROFILE_POINTS)
    target_compile_definitions(wa2_port PRIVATE WA2_NATIVE_PROFILE_POINTS=1)
endif()
# The main execution path is generated native assembly. Keep its complete
# checked C++ fallback, but compile those cold pages for size.
set_source_files_properties(${WA2_NATIVE_ORACLES} DIRECTORY "${WA2_PROJECT_ROOT}/generated/wa2-win64" "${CMAKE_CURRENT_SOURCE_DIR}"
    PROPERTIES COMPILE_OPTIONS "-Oz;-mllvm;-enable-machine-outliner=always")
file(MAKE_DIRECTORY "${WA2_PROJECT_ROOT}/build/symbols")
add_executable(wa2_heap_churn_tests "${WA2_PROJECT_ROOT}/tests/heap_churn.cpp")
target_include_directories(wa2_heap_churn_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(wa2_heap_churn_tests PRIVATE winrecomp_guest)
add_executable(wa2_pak_loading_tests "${WA2_PROJECT_ROOT}/tests/pak_loading.cpp"
    "${WA2_PROJECT_ROOT}/generated/wa2-win64/code_page_1075.cpp"
    "${WA2_PROJECT_ROOT}/generated/wa2-win64/code_page_1076.cpp"
    "${WA2_PROJECT_ROOT}/generated/wa2-win64/code_page_1079.cpp"
    "${WA2_PROJECT_ROOT}/generated/wa2-win64/code_page_1111.cpp"
    "${WA2_PROJECT_ROOT}/generated/wa2-win64/code_page_1112.cpp"
    "${WA2_PROJECT_ROOT}/generated/wa2-win64/code_page_1141.cpp"
    "${WA2_PROJECT_ROOT}/generated/wa2-win64/code_page_1150.cpp")
target_link_libraries(wa2_pak_loading_tests PRIVATE wa2_port)
add_executable(wa2_block_tests "${CMAKE_CURRENT_SOURCE_DIR}/../tests/static_blocks.cpp" ${WA2_STATIC_BLOCK_ORACLES})
target_link_libraries(wa2_block_tests PRIVATE wa2_port wa2_block_reference)
add_executable(wa2_native_tests "${CMAKE_CURRENT_SOURCE_DIR}/../tests/native_backend.cpp" ${WA2_STATIC_BLOCK_ORACLES}
    "${WA2_PROJECT_ROOT}/generated/wa2-win64/code_page_1090.cpp"
    "${WA2_PROJECT_ROOT}/generated/wa2-win64/code_page_1133.cpp"
    "${WA2_PROJECT_ROOT}/generated/wa2-win64/code_page_1141.cpp"
    "${WA2_PROJECT_ROOT}/generated/wa2-win64/code_page_1148.cpp"
    "${WA2_PROJECT_ROOT}/generated/wa2-win64/code_page_1149.cpp")
target_link_libraries(wa2_native_tests PRIVATE wa2_port)
add_executable(wa2_shift_tests "${CMAKE_CURRENT_SOURCE_DIR}/../tests/shift_fast.cpp")

target_link_libraries(wa2_shift_tests PRIVATE winrecomp_guest)
add_executable(wa2_port_tests "${CMAKE_CURRENT_SOURCE_DIR}/../tests/port.cpp")
add_executable(wa2_script_tests "${CMAKE_CURRENT_SOURCE_DIR}/../tests/script_handlers.cpp"
    "${WA2_PROJECT_ROOT}/generated/wa2-win64/code_page_1025.cpp"
    "${WA2_PROJECT_ROOT}/generated/wa2-win64/code_page_1076.cpp"
    "${WA2_PROJECT_ROOT}/generated/wa2-win64/code_page_1108.cpp"
    "${WA2_PROJECT_ROOT}/generated/wa2-win64/code_page_1109.cpp"
    "${WA2_PROJECT_ROOT}/generated/wa2-win64/code_page_1110.cpp"
    "${WA2_PROJECT_ROOT}/generated/wa2-win64/code_page_1112.cpp"
    "${WA2_PROJECT_ROOT}/generated/wa2-win64/code_page_1118.cpp"
    "${WA2_PROJECT_ROOT}/generated/wa2-win64/code_page_1119.cpp"
    "${WA2_PROJECT_ROOT}/generated/wa2-win64/code_page_1121.cpp")
target_link_libraries(wa2_script_tests PRIVATE wa2_port)
target_link_libraries(wa2_port_tests PRIVATE wa2_port)
add_executable(wa2_graphics_tests "${CMAKE_CURRENT_SOURCE_DIR}/../tests/graphics.cpp")
add_executable(wa2_audio_buffer_tests "${WA2_PROJECT_ROOT}/tests/audio_buffers.cpp")
target_link_libraries(wa2_audio_buffer_tests PRIVATE wa2_port)
add_executable(wa2_resource_memory_tests "${WA2_PROJECT_ROOT}/tests/resource_memory.cpp"
    "${WA2_PROJECT_ROOT}/tools/WindowsRecompTool-main/src/runtime/native_memory.cpp")
target_include_directories(wa2_resource_memory_tests PRIVATE "${WA2_PROJECT_ROOT}/tools/WindowsRecompTool-main/include")
target_link_libraries(wa2_graphics_tests PRIVATE wa2_port)
add_executable(wa2_media_tests "${CMAKE_CURRENT_SOURCE_DIR}/../tests/media.cpp" "${WA2_PROJECT_ROOT}/generated/wa2-win64/code_page_1099.cpp")
add_executable(wa2_cpu_feature_tests "${CMAKE_CURRENT_SOURCE_DIR}/../tests/cpu_features.cpp"
    "${WA2_PROJECT_ROOT}/generated/wa2-win64/code_page_1152.cpp" "${WA2_PROJECT_ROOT}/generated/wa2-win64/code_page_1167.cpp"
    "${WA2_PROJECT_ROOT}/generated/wa2-win64/code_page_1148.cpp")
target_link_libraries(wa2_cpu_feature_tests PRIVATE wa2_port)
add_executable(wa2_device_tests "${CMAKE_CURRENT_SOURCE_DIR}/../tests/device_change.cpp")
target_link_libraries(wa2_device_tests PRIVATE wa2_port)
target_link_libraries(wa2_media_tests PRIVATE wa2_port)
add_executable(wa2_thread_tests "${CMAKE_CURRENT_SOURCE_DIR}/../tests/threads.cpp")
target_link_libraries(wa2_thread_tests PRIVATE wa2_port)
add_executable(wa2_fast_tests "${CMAKE_CURRENT_SOURCE_DIR}/../tests/fast_paths.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/../generated/wa2-win64/code_page_1079.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/../generated/wa2-win64/code_page_1090.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/../generated/wa2-win64/code_page_1124.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/../generated/wa2-win64/code_page_1150.cpp")
target_link_libraries(wa2_fast_tests PRIVATE wa2_port)
add_executable(wa2_vorbis_tests "${CMAKE_CURRENT_SOURCE_DIR}/../tests/vorbis_batch.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/../generated/wa2-win64/code_page_1134.cpp")
target_link_libraries(wa2_vorbis_tests PRIVATE wa2_port)
add_executable(wa2_vorbis_butterfly_tests "${CMAKE_CURRENT_SOURCE_DIR}/../tests/vorbis_butterfly.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/../generated/wa2-win64/code_page_1134.cpp")
target_link_libraries(wa2_vorbis_butterfly_tests PRIVATE wa2_port)
target_sources(wa2_port PRIVATE uv_batch.cpp uv_batch.S)
add_executable(wa2_uv_tests "${CMAKE_CURRENT_SOURCE_DIR}/../tests/uv_batch.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/../generated/wa2-win64/code_page_1084.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/../generated/wa2-win64/code_page_1085.cpp")
target_link_libraries(wa2_uv_tests PRIVATE wa2_port)
target_sources(wa2_port PRIVATE fp_sequences.cpp fp_sequences.S)
add_executable(wa2_fp_sequence_tests "${CMAKE_CURRENT_SOURCE_DIR}/../tests/fp_sequences.cpp" ${WA2_FP_ORACLES})
target_link_libraries(wa2_fp_sequence_tests PRIVATE wa2_port)
target_sources(wa2_port PRIVATE sprite_transform.cpp sprite_transform.S)
add_executable(wa2_sprite_transform_tests "${CMAKE_CURRENT_SOURCE_DIR}/../tests/sprite_transform.cpp" "${WA2_PROJECT_ROOT}/generated/wa2-win64/code_page_1083.cpp")
target_link_libraries(wa2_sprite_transform_tests PRIVATE wa2_port)
target_sources(wa2_port PRIVATE bitreader_batch.cpp)
add_executable(wa2_bitreader_tests "${WA2_PROJECT_ROOT}/tests/bitreader_batch.cpp" "${WA2_PROJECT_ROOT}/generated/wa2-win64/code_page_1183.cpp")
target_link_libraries(wa2_bitreader_tests PRIVATE wa2_port)
add_executable(wa2_color_tests "${CMAKE_CURRENT_SOURCE_DIR}/../tests/color_batch.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/../generated/wa2-win64/code_page_1083.cpp")
target_link_libraries(wa2_color_tests PRIVATE wa2_port)
add_executable(wa2_vertex_tests "${CMAKE_CURRENT_SOURCE_DIR}/../tests/vertex_batch.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/../generated/wa2-win64/code_page_1083.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/../generated/wa2-win64/code_page_1084.cpp")

target_link_libraries(wa2_vertex_tests PRIVATE wa2_port)
add_executable(wa2_win64_tests "${CMAKE_CURRENT_SOURCE_DIR}/../tests/win64.cpp")
target_link_libraries(wa2_win64_tests PRIVATE wa2_port)
add_executable(wa2_timing_tests "${WA2_PROJECT_ROOT}/tests/timing.cpp")
target_link_libraries(wa2_timing_tests PRIVATE wa2_port)
# The production executable must not pull old codec instruction kernels back
# through the generic fast-path dispatcher. Comparison executables keep them.
target_sources(recompiled_program PRIVATE retired_codec_stub.cpp fp_sequences_release.cpp)
add_library(wa2_codec_oracle STATIC legacy_vorbis_kernels.cpp vorbis_batch.cpp vorbis_batch.S vorbis_butterfly.cpp vorbis_butterfly.S)
target_link_libraries(wa2_codec_oracle PUBLIC winrecomp_guest)
target_include_directories(wa2_codec_oracle PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
add_dependencies(wa2_codec_oracle wa2_generated_batches)
get_property(WA2_PORT_TARGETS DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(target IN LISTS WA2_PORT_TARGETS)
    get_target_property(type ${target} TYPE)
    if(type STREQUAL "EXECUTABLE")
        target_link_libraries(${target} PRIVATE wa2_codec_oracle)
    endif()
endforeach()
include("${CMAKE_CURRENT_SOURCE_DIR}/compact_fallback.cmake")
