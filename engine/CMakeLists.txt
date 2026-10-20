cmake_minimum_required(VERSION 3.20)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(WA2NativeEngine LANGUAGES CXX)
endif()
add_library(wa2_engine STATIC pcm_audio.cpp pcm_stream.cpp resource_archive.cpp resource_catalog.cpp vorbis_stream.cpp ogg_reader.cpp wave_reader.cpp stream_voice.cpp raster_image.cpp image_store.cpp font_glyphs.cpp text_layout.cpp subtitles.cpp)
target_compile_features(wa2_engine PUBLIC cxx_std_20)
target_include_directories(wa2_engine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
add_library(wa2_pixel_transfer STATIC pixel_transfer.cpp)
target_compile_features(wa2_pixel_transfer PUBLIC cxx_std_20)
target_include_directories(wa2_pixel_transfer PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
set_target_properties(wa2_pixel_transfer PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(wa2_scalar_math STATIC scalar_math.cpp trigonometry.cpp crt_trigonometry.cpp)
target_compile_features(wa2_scalar_math PUBLIC cxx_std_20)
target_include_directories(wa2_scalar_math PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
set_target_properties(wa2_scalar_math PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    # Decode and callback mixing are latency-sensitive on mobile CPUs.
    # Preserve operation order; never trade PCM values for fast-math.
    set_source_files_properties(pcm_audio.cpp vorbis_stream.cpp PROPERTIES COMPILE_OPTIONS "-O3;-ffp-contract=off")
    set_source_files_properties(resource_archive.cpp PROPERTIES COMPILE_OPTIONS "-O3")
    target_compile_options(wa2_scalar_math PRIVATE -O2 -ffp-contract=off -frounding-math)
    # One ordinary byte-conversion loop benefits from vectorization on both
    # x64 and ARM64; the generated game bodies retain their size settings.
    target_compile_options(wa2_pixel_transfer PRIVATE -O3)
endif()
add_library(wa2_script STATIC script_runtime.cpp script_session.cpp scene_controls.cpp scene_music.cpp scene_effects.cpp scene_background.cpp scene_movie.cpp scene_weather.cpp scene_dialogue.cpp scene_colour.cpp scene_bitmaps.cpp scene_characters.cpp scene_voices.cpp)
target_compile_features(wa2_script PUBLIC cxx_std_20)
target_include_directories(wa2_script PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(wa2_script PUBLIC wa2_engine)
# Builds without the legacy recompiler, Windows headers or a platform backend.
add_executable(wa2_pcm_audio_tests "${CMAKE_CURRENT_SOURCE_DIR}/../tests/pcm_audio.cpp")
target_link_libraries(wa2_pcm_audio_tests PRIVATE wa2_engine)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    find_package(Threads REQUIRED)
    target_link_libraries(wa2_engine PUBLIC Threads::Threads)
    include("${CMAKE_CURRENT_SOURCE_DIR}/../platform/portable.cmake")
    enable_testing()
    add_test(NAME native_pcm COMMAND wa2_pcm_audio_tests "${CMAKE_BINARY_DIR}/native-pcm-tests.json")
    add_test(NAME native_resources COMMAND wa2_resource_tests
        "${CMAKE_CURRENT_SOURCE_DIR}/../res"
        "${CMAKE_CURRENT_SOURCE_DIR}/../build/resource-load/portable-samples.bin"
        "${CMAKE_BINARY_DIR}/native-resource-tests.json")
    add_executable(wa2_vorbis_stream_tests "${CMAKE_CURRENT_SOURCE_DIR}/../tests/vorbis_portable.cpp")
    target_link_libraries(wa2_vorbis_stream_tests PRIVATE wa2_platform)
    add_test(NAME native_vorbis COMMAND wa2_vorbis_stream_tests
        "${CMAKE_CURRENT_SOURCE_DIR}/../res" "${CMAKE_BINARY_DIR}/native-vorbis-tests.json")
endif()
