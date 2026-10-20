"""Verify the SAME static function sources on x64 and Android ARM64, headless.

No game, emulator, screen, audio device, original EXE, or parallel build jobs.
The Android result is a linked native check library, not an Android play test.
"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--differential-receipt',type=Path,
        help='Reuse a successful independent memory run only when its exact binary and report still match')
    args=parser.parse_args()
    kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    kernel.GetCurrentProcess.restype=ctypes.c_void_p
    kernel.SetPriorityClass.argtypes=[ctypes.c_void_p,ctypes.c_uint]
    if not kernel.SetPriorityClass(kernel.GetCurrentProcess(),0x4000):
        raise ctypes.WinError(ctypes.get_last_error())
    # Headless component checks must return an error code instead of waiting
    # for an operating-system fault dialog outside the test timeout.
    kernel.SetErrorMode(0x8003)
    environment=os.environ.copy()
    environment.update(CMAKE_BUILD_PARALLEL_LEVEL='1',TEMP=str(ROOT/'build/temp'),TMP=str(ROOT/'build/temp'),
                       ZIG_GLOBAL_CACHE_DIR=str(ROOT/'build/zig-cache'),ZIG_LOCAL_CACHE_DIR=str(ROOT/'build/zig-local'))
    flags=subprocess.BELOW_NORMAL_PRIORITY_CLASS|subprocess.CREATE_NO_WINDOW
    build=ROOT/'build/shared-aot-x64';android=ROOT/'build/shared-aot-android'
    build.mkdir(parents=True,exist_ok=True);android.mkdir(parents=True,exist_ok=True)
    cmake=ROOT/'tools/python/cmake/data/bin/cmake.exe'
    ndk=ROOT/'tools/android-sdk/ndk/27.0.12077973'
    acmake=ROOT/'tools/android-sdk/cmake/3.22.1/bin/cmake.exe'
    console=(ROOT/'logs/shared-aot-build.log').open('wb')
    def run(command,timeout=180):
        command=[str(s) for s in command]
        with subprocess.Popen(command,cwd=ROOT,env=environment,creationflags=flags,
                              stdout=console,stderr=subprocess.STDOUT) as child:
            try:code=child.wait(timeout)
            except subprocess.TimeoutExpired:
                # Killing only cmake leaves its compiler consuming resources.
                # This PID is the child just created here, never a name match.
                subprocess.run(['taskkill','/PID',str(child.pid),'/T','/F'],creationflags=flags,
                               stdout=console,stderr=subprocess.STDOUT,timeout=20)
                child.wait(20)
                raise
            if code:raise subprocess.CalledProcessError(code,command)
    # Build the SDL adapter from this configuration, rather than accepting a
    # cached library whose feature set can disagree with SDLActivity's JNI.
    sdl_build=ROOT/'build/android-native-game'
    run([acmake,'-S',ROOT/'android','-B',sdl_build,'-G','Ninja',
         '-DCMAKE_MAKE_PROGRAM='+str(ROOT/'tools/android-sdk/cmake/3.22.1/bin/ninja.exe'),
         '-DCMAKE_TOOLCHAIN_FILE='+str(ndk/'build/cmake/android.toolchain.cmake'),
         '-DANDROID_ABI=arm64-v8a','-DANDROID_PLATFORM=android-26','-DANDROID_STL=c++_shared',
         '-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON','-DCMAKE_BUILD_TYPE=MinSizeRel'])
    run([acmake,'--build',sdl_build,'--target','SDL2','--parallel','1'],1200)
    run([sys.executable,ROOT/'tests/aot_liveness.py'])
    run([sys.executable,ROOT/'scripts/prepare_subtitles.py','--check'])
    run([sys.executable,ROOT/'scripts/verify_fp_kernel.py'],600)
    run([sys.executable,ROOT/'scripts/generate_shared_aot.py'])
    run([sys.executable,ROOT/'scripts/prepare_static_data.py'])
    run([sys.executable,ROOT/'scripts/generate_trigonometry_tables.py'])
    run([sys.executable,ROOT/'scripts/generate_codepages.py'])
    run([sys.executable,ROOT/'scripts/prepare_app_icon.py'])
    run([sys.executable,ROOT/'scripts/verify_portable_shaders.py'],900)
    tracked=[p for directory in ('recompiled','generated/shared-aot','generated/static-data','generated/codepages') for p in (ROOT/directory).glob('*') if p.is_file()]
    tracked += [ROOT/'tests/shared_aot.cpp',ROOT/'tests/function_process.cpp',ROOT/'tests/portable_scheduler.cpp',Path(__file__),ROOT/'config/porting_contract.json',ROOT/'scripts/prepare_static_data.py']
    tracked += [ROOT/'tests/aot_liveness.py']
    tracked += [ROOT/'tests/subtitles.cpp',ROOT/'scripts/prepare_subtitles.py',ROOT/'scripts/verify_subtitles.py',ROOT/'config/subtitle_hooks.json']
    tracked += [p for p in (ROOT/'third_party/wa2_subtitles').glob('*') if p.is_file()]
    tracked += [ROOT/'tests/texture_batch_snapshots.hpp']
    tracked += [ROOT/'tests/portrait_submission_snapshots.hpp']
    tracked += [ROOT/'tests/dialogue_cpu.cpp',ROOT/'tests/geometry_cache_snapshots.hpp']
    tracked += [ROOT/'tests/native_sprite_cpu.cpp',ROOT/'config/native_render_contracts.json']
    tracked += [ROOT/'tests/particle_pipeline.cpp',ROOT/'tests/native_resource_cache.cpp']
    tracked += [ROOT/'scripts/verify_vertex_grid.py']
    tracked += [ROOT/'scripts/verify_fast_dispatch.py']
    tracked += [ROOT/'scripts/compare_particle_cpu.py']
    tracked += [ROOT/'scripts/summarize_particle_performance.py']
    tracked += [ROOT/'scripts/generate_trigonometry_tables.py']
    tracked += [ROOT/'android/CMakeLists.txt',ROOT/'scripts/verify_sdl_jni.py']
    tracked += list((ROOT/'tools/SDL-release-2.32.10/android-project/app/src/main/java/org/libsdl/app').glob('*.java'))
    tracked += [ROOT/'config/release.json']
    tracked += [ROOT/'icon.ico',ROOT/'scripts/prepare_app_icon.py']
    tracked += [p for p in (ROOT/'android/app/src/main/res').rglob('*') if p.is_file()]
    tracked += [ROOT/'config/aot_hot_functions.json']
    tracked += [ROOT/'tests/native_loop_snapshots.hpp',ROOT/'tests/portable_registry.cpp',ROOT/'config/native_loops.json']
    tracked += [ROOT/'scripts/generate_weather_batch.py']
    tracked += [ROOT/'tests/portable_system.cpp',ROOT/'scripts/generate_codepages.py',ROOT/'port/exceptions.cpp',ROOT/'port/exception_abi.hpp']
    tracked += [ROOT/'tests/resource_mount.cpp']
    tracked += [ROOT/'tests/native_resource_memory.cpp']
    tracked += [ROOT/'tests/resource_loading.cpp',ROOT/'tests/wait_probes.cpp']
    tracked += [ROOT/'tests/lzss_groups.cpp',ROOT/'scripts/compare_lzss_groups.py']
    tracked += [ROOT/'tests/glyph_index.cpp',ROOT/'tests/glyph_geometry.cpp',ROOT/'tests/byte_equality.cpp',ROOT/'scripts/compare_glyph_cpu.py']
    tracked += [ROOT/'tests/heap_pool_memory.cpp']
    tracked += [ROOT/'tests/large_heap_memory.cpp']
    tracked += [ROOT/'scripts/verify_android_java.py',ROOT/'tests/android/org/wa2/port/ResourceLocationTest.java',ROOT/'android/app/src/main/AndroidManifest.xml']
    tracked += [ROOT/'tests/android/org/wa2/port/WidgetControlsTest.java']
    tracked += [ROOT/'tests/android/org/wa2/port/WidgetIconTest.java']
    tracked += [ROOT/'tests/android/org/wa2/port/TouchGesturesTest.java']
    tracked += [ROOT/'tests/portable_sound_bridge.cpp',ROOT/'port/audio.hpp']
    tracked += [ROOT/'tests/window_resources.cpp']
    tracked += [ROOT/'tests/widget_unicode.cpp',ROOT/'scripts/verify_widget_unicode.py']
    tracked += [ROOT/'tests/window_model.cpp']
    tracked += [ROOT/'tests/render_submission.cpp']
    tracked += [ROOT/'tests/host_leaf_calls.cpp',ROOT/'tests/render_cpu.cpp']
    tracked += [ROOT/'tests/integer_registers.cpp']
    tracked += [ROOT/('tests/'+name) for name in ('native_string.cpp','weather_cpu.cpp','interface_handles.cpp','audio_latency.cpp')]
    tracked += [ROOT/'tests/image_colours.cpp',ROOT/'tests/native_memory.cpp',ROOT/'tests/image_upload.cpp',ROOT/'tests/image_upload_before.cpp']
    tracked += [ROOT/'tests/movie_tracks.cpp']
    tracked += [ROOT/('scripts/'+name) for name in ('prepare_weather_baseline.py','verify_particle_audio.py','prepare_particle_baseline.py','verify_particles.py')]
    baseline=ROOT/'build/performance-0.2.19'
    tracked += [baseline/name for name in ('baseline.json','before-pcm_audio.cpp','before-vorbis_stream.cpp',
        'before-shared-aot-verification.json','weather-baseline-link.json')]
    tracked += [ROOT/name for name in json.loads((baseline/'weather-baseline-link.json').read_text())['sha256']]
    particle_baseline=ROOT/'build/performance-0.2.20'
    tracked += [particle_baseline/name for name in ('before-shared-aot-verification.json','weather-baseline-link.json')]
    tracked += [ROOT/name for name in json.loads((particle_baseline/'weather-baseline-link.json').read_text())['sha256']]
    tracked += [ROOT/('tests/'+name) for name in ('portable_fp.cpp','portable_simd.cpp','fp_execution.cpp','fp_workload.cpp')]
    tracked += [ROOT/'tests/fp-kernel/CMakeLists.txt',ROOT/'scripts/verify_fp_kernel.py',ROOT/'scripts/generate_fp_kernel_cases.py']
    tracked += [ROOT/'tests/crt_math_paths.cpp']
    tracked += [ROOT/'tests/pixel_transfer.cpp']
    tracked += [ROOT/'config/native_math_contracts.json']
    tracked += [ROOT/('tests/'+name) for name in ('portable_movie_bridge.cpp','portable_acm.cpp','portable_shell.cpp')]
    tracked += [p for p in (ROOT/'hosts').rglob('*') if p.is_file()]
    tracked += [p for p in (ROOT/'android/app/src/main/java').rglob('*.java')]
    tracked += [p for p in (ROOT/'android/native').glob('*') if p.suffix in ('.cpp','.hpp')]
    tracked += [ROOT/('tools/media-codec/'+host+'/lib/'+name) for host in ('windows','arm64')
                for name in ('libavformat.a','libavcodec.a','libavutil.a')]
    tracked += [ROOT/'build/android-native-game/sdl2/libSDL2.so',ROOT/'build/wa2-win64/wa2_port/sdl2/libSDL2.a']
    tracked += [ROOT/'scripts/capture_nls_snapshot.py',ROOT/'port/audio.cpp',ROOT/'port/audio_abi.hpp',ROOT/'port/audio_tables.inc']
    tracked += [ROOT/('android/native/'+name) for name in ('android_resources.cpp','android_resources.hpp','compiled_resources.cpp','compiled_resources.hpp','resource_jni.hpp')]
    tracked += [ROOT/('port/'+name) for name in ('sprite_sort.cpp','sprite_sort.hpp','sort_signature.inc','vertex_batch.cpp','vertex_batch.hpp','vertex_signature.inc')]
    tracked += [ROOT/'tests/shared-aot-android/CMakeLists.txt',ROOT/'tests/shared-aot-android/entry.cpp']
    tracked += list((ROOT/'tools/WindowsRecompTool-main/include/winrecomp').glob('*.hpp'))
    tracked += [ROOT/('tools/WindowsRecompTool-main/src/'+name) for name in
                ('runtime/native_memory.cpp','runtime/process.cpp','runtime/win32.cpp','pe.cpp','resources.cpp')]
    tracked += [p for directory in ('engine','platform') for p in (ROOT/directory).glob('*') if p.suffix in ('.cpp','.hpp','.inc','.cmake','.txt')]
    tracked += [p for p in (ROOT/'platform/compat').glob('*') if p.suffix in ('.cpp','.hpp','.cmake')]
    tracked += [p for p in (ROOT/'platform/window').glob('*') if p.suffix in ('.cpp','.hpp','.cmake')]
    tracked += [ROOT/'android/native/widgets_android.cpp',ROOT/'android/native/widgets_jni.hpp']
    tracked += [p for p in (ROOT/'platform/render').glob('*') if p.suffix in ('.cpp','.hpp','.h','.cmake')]
    tracked += [ROOT/'tools/mojoshader-main/mojoshader_opengl.c',ROOT/'port/d3d9_tables.inc']
    tracked += list((ROOT/'generated/graphics-abi').glob('*'))
    tracked += [ROOT/('port/'+name) for name in ('native_resources.cpp','native_resources.hpp','native_vorbis.cpp','native_vorbis.hpp','guest_api.hpp','native_backend.hpp','registry.cpp','wa2_win32.hpp')]
    bindings={p.relative_to(ROOT).as_posix():digest(p) for p in tracked}
    if args.differential_receipt:
        receipt=args.differential_receipt.resolve()
        if not receipt.is_relative_to(ROOT):raise ValueError('Differential receipt is outside the workspace')
        bindings[receipt.relative_to(ROOT).as_posix()]=digest(receipt)
    shader_proof=json.loads((ROOT/'logs/portable-shader-verification.json').read_text())
    if not shader_proof['passed'] or not shader_proof['checks'].get('offline_glsl_compile_and_link_verified'):raise ValueError('Missing offline GLSL compile/link verification')
    # Bind the generic assembler/parser and validator too: packaging must never
    # accept a proof produced before those shader inputs were changed.
    bindings.update(shader_proof['source_sha256'])
    run([cmake,'-S',ROOT/'recompiled','-B',build,'-G','Ninja',
         '-DCMAKE_TOOLCHAIN_FILE='+str(ROOT/'scripts/zig-toolchain.cmake'),
         '-DCMAKE_BUILD_TYPE=MinSizeRel','-DWA2_SHARED_AOT_CHECKS=ON','-DWA2_AOT_DIAGNOSTICS=ON','-DCMAKE_EXPORT_COMPILE_COMMANDS=ON'])
    run([cmake,'--build',build,'--target','wa2_shared_aot_tests','wa2_function_process_tests','wa2_portable_scheduler_tests','wa2_portable_registry_tests','wa2_portable_system_tests','wa2_resource_mount_tests','wa2_native_resource_memory_tests','wa2_native_resource_cache_tests','wa2_resource_loading_tests','wa2_lzss_group_tests','wa2_wait_probe_tests','wa2_large_heap_memory_tests','wa2_portable_sound_bridge_tests','wa2_window_resource_tests','wa2_widget_unicode_tests','wa2_window_model_tests','wa2_portable_movie_bridge_tests','wa2_render_submission_tests','wa2_portable_acm_tests','wa2_portable_shell_tests','wa2_crt_math_path_tests','wa2_pixel_transfer_tests','--parallel','1'],2400)
    run([cmake,'--build',build,'--target','wa2_glyph_index_tests','wa2_glyph_geometry_tests','wa2_byte_equality_tests','--parallel','1'],300)
    run([cmake,'--build',build,'--target','wa2_subtitle_tests','--parallel','1'],300)
    subtitle_report=build/'subtitles.json'
    run([build/'wa2_subtitle_tests.exe',subtitle_report],30)
    from verify_subtitles import verify as verify_subtitles
    subtitle_checks=verify_subtitles(subtitle_report,ROOT/'generated/shared-aot/manifest.json')
    submission_report=build/'render-submission.json'
    run([build/'wa2_render_submission_tests.exe',submission_report],30)
    run([cmake,'--build',build,'--target','wa2_host_leaf_tests','wa2_render_cpu_tests','wa2_dialogue_cpu_tests','wa2_native_sprite_cpu_tests','wa2_particle_pipeline_tests','wa2_integer_register_tests','wa2_heap_pool_memory_tests','wa2_image_upload_tests',
        'wa2_native_string_tests','wa2_native_memory_tests','wa2_image_colour_tests','wa2_movie_track_tests','wa2_weather_cpu_tests','wa2_interface_handle_tests','wa2_audio_latency_tests','wa2_audio_latency_before','--parallel','1'],600)
    string_report=build/'native-string.json'
    run([build/'wa2_native_string_tests.exe',string_report],30)
    buffer_report=build/'native-memory.json'
    run([build/'wa2_native_memory_tests.exe',buffer_report],90)
    buffers=json.loads(buffer_report.read_text())
    if (not buffers['passed'] or buffers['large_string_cases']!=16 or buffers['precise_partial_fault_cases']!=16 or
        buffers['integrated_clear_cases']!=10 or buffers['unchanged_declines']!=16 or buffers['single_clear_fallback_attempts']!=8):
        raise ValueError('Large resource buffers changed CPU/FP/RAM, fault prefixes or one-shot fallback behavior')
    interface_report=build/'interface-handles.json'
    run([build/'wa2_interface_handle_tests.exe',interface_report],30)
    run([sys.executable,ROOT/'scripts/verify_particle_audio.py'],600)
    version=json.loads((ROOT/'config/release.json').read_text())['name'].removesuffix('-shared-aot')
    particle_audio_report=ROOT/f'logs/performance-particle-audio-{version}.json'
    run([sys.executable,ROOT/'scripts/verify_particles.py'],600)
    particle_report=ROOT/f'logs/performance-particles-{version}.json'
    leaf_report=build/'host-leaf.json'
    run([build/'wa2_host_leaf_tests.exe',leaf_report],30)
    integer_report=build/'integer-registers.json'
    run([build/'wa2_integer_register_tests.exe',integer_report],30)
    render_cpu_report=build/'render-cpu.json'
    dialogue_report=build/'dialogue-cpu.json'
    run([build/'wa2_dialogue_cpu_tests.exe',dialogue_report],30)
    dialogue=json.loads(dialogue_report.read_text())
    if (not dialogue['passed'] or dialogue['exact_source_variants']!=12 or dialogue['cache_key_checks']<237 or
        dialogue['unrelated_input_variants']!=4 or dialogue['noise_grid_hits']<60 or not dialogue['changing_lines_skip_capture'] or len(dialogue['noisy_paired_ns'])!=5 or
        not dialogue['cached_outputs_bit_exact'] or not dialogue['memo_grid_hits'] or not dialogue['memo_sprite_hits'] or
        dialogue['source_draws']!=46 or dialogue['recorded_driver_packets']!=1):
        raise ValueError('Cached source dialogue geometry or submission replay failed')
    glyph=dialogue['native_glyph_chain']
    if (glyph['exact_source_variants']!=12 or glyph['accepted_glyphs']!=480 or glyph['legacy_api_calls'] or
        glyph['changing_source_cases']!=16 or len(glyph['changing_native_ns'])!=5 or len(glyph['memo_enabled_pairs'])!=5 or
        glyph['memo_finds']!=1600 or glyph['memo_hits']<800 or not glyph['memo_captures'] or
        not glyph['raw_vertex_bits_equal'] or not glyph['void_cdecl_abi_preserved'] or len(glyph['paired_ns'])!=5):
        raise ValueError('Complete native glyph path did not preserve the cached drawing output')
    sprite_report=build/'native-sprite-cpu.json'
    run([build/'wa2_native_sprite_cpu_tests.exe',sprite_report],60)
    sprite=json.loads(sprite_report.read_text())
    if (not sprite['passed'] or sprite['cases']!=256 or sprite.get('visible_grid_cases')!=48 or sprite['decline_cases']!=24 or sprite['renderer_decline_cases']!=1 or sprite['host_environment_cases']!=4 or
        not sprite['vertex_bits_and_render_state_equal'] or not sprite['void_cdecl_abi_preserved'] or
        len(sprite['timings'])!=3 or any(len(item['paired_ns'])!=5 or any(min(pair)<=0 for pair in item['paired_ns']) for item in sprite['timings'])):
        raise ValueError('Complete native sprite/particle drawing comparison failed')
    pipeline_report=build/'particle-pipeline.json'
    run([build/'wa2_particle_pipeline_tests.exe',pipeline_report,'--require-native'],90)
    pipeline=json.loads(pipeline_report.read_text())
    if (not pipeline['passed'] or pipeline['cases']!=12 or pipeline['source_draws']!=3264 or
        pipeline['native_admitted']!=3264 or pipeline['native_offscreen_draws']!=2176 or not pipeline['source_vertices_and_state_equal']):
        raise ValueError('Actual cached weather sprites did not reach the native offscreen/filter path')
    # The cache deliberately leaves segment and SIMD state untouched. These
    # proved regions may not gain such operations without a new cache contract.
    for name in ('sprite_geometry.inc','vertex_grid.inc'):
        if re.search(r'sse_|xmm_|fs_base|gs_base|selectors|wr::(?:reg|set_reg)\(s,5,', (ROOT/'generated/shared-aot'/name).read_text()):
            raise ValueError('Geometry cache register-write contract changed: '+name)
    run([build/'wa2_render_cpu_tests.exe',render_cpu_report],30)
    render_cpu_24_report=build/'render-cpu-24bit.json'
    run([build/'wa2_render_cpu_tests.exe',render_cpu_24_report,'--pc24'],30)
    render_dense_report=build/'render-cpu-dense.json'
    run([build/'wa2_render_cpu_tests.exe',render_dense_report,'--pc24','--dense'],30)
    portrait_report=build/'portrait-texture-batch.json'
    run([build/'wa2_render_cpu_tests.exe',portrait_report,'--pc24','--dense','--varied','--texture-batches'],30)
    portrait=json.loads(portrait_report.read_text())['source_texture_batch_replay']
    if not portrait['enabled'] or not portrait['distinct_source_tiles'] or (portrait['source_draws'],portrait['accepted_draws'],portrait['recorded_driver_packets'])!=(40,40,5):
        raise ValueError('Cached source sprite draws did not enter the bounded multi-texture path')
    packets=json.loads(submission_report.read_text())['multi_texture_batch']
    if (packets['source_draws'],packets['recorded_driver_packets'])!=(120,15) or not packets['unchanged_vertex_bits_uv_colour_and_order']:
        raise ValueError('Texture packet ordering, lifetime or vertex-data proof is incomplete')
    if shader_proof['checks'].get('texture_batch_variants',0)<32 or not shader_proof['checks'].get('texture_batch_explicit_derivatives'):
        raise ValueError('Multi-texture desktop/GLES derivative/sampler shader checks are missing')
    render_varied_report=build/'render-cpu-gradient.json'
    run([build/'wa2_render_cpu_tests.exe',render_varied_report,'--pc24','--dense','--varied'],30)
    render_masked_report=build/'render-cpu-masked.json'
    run([build/'wa2_render_cpu_tests.exe',render_masked_report,'--pc24','--masked','--varied'],30)
    render_serialized_report=build/'render-cpu-serialized.json'
    run([build/'wa2_render_cpu_tests.exe',render_serialized_report,'--pc24','--dense','--varied','--serialized'],30)
    run([sys.executable,ROOT/'scripts/verify_fast_dispatch.py','--no-build'],120)
    pixel_report=build/'pixel-transfer.json'
    run([build/'wa2_pixel_transfer_tests.exe',pixel_report],30)
    upload_report=build/'image-upload.json'
    run([build/'wa2_image_upload_tests.exe',upload_report],60)
    image_upload=json.loads(upload_report.read_text())
    if (not image_upload['passed'] or image_upload['exact_rectangle_cases']!=320 or image_upload['late_faults_unchanged']!=4 or
        image_upload['unreadable_padding_cases']!=4 or image_upload['unchanged_declines']!=10 or image_upload['bgr24_overlap_cases']!=12 or
        len(image_upload['timings'])!=4 or any(len(item['paired_ns'])!=5 or item['full_image_staging_bytes_after']!=0 for item in image_upload['timings'])):
        raise ValueError('Direct image loading changed pixel, crop, padding, source fault or staging behavior')
    image_colour_report=build/'image-colours.json'
    run([build/'wa2_image_colour_tests.exe',image_colour_report],90)
    image_colours=json.loads(image_colour_report.read_text())
    if (not image_colours['passed'] or image_colours['exact_loop_cases']!=324 or image_colours['admitted_cases']!=216 or
        image_colours['integrated_static_resumes']!=12 or image_colours['decline_cases']!=240 or
        image_colours['precise_fault_cases']<36 or image_colours['partial_store_prefixes']<12 or image_colours['single_fallback_attempts']!=72 or
        not image_colours['same_generated_static_resumes'] or not image_colours['rgba_alpha_and_padding_exact'] or
        len(image_colours['timings'])!=4 or any(len(item['paired_ns'])!=5 or any(min(pair)<=0 for pair in item['paired_ns']) for item in image_colours['timings'])):
        raise ValueError('Shared image-colour loops changed pixels, ABI, source resume or precise fault prefixes')
    crt_math_report=build/'crt-math-paths.json'
    run([build/'wa2_crt_math_path_tests.exe',crt_math_report],30)
    report=build/'differential.json'
    if args.differential_receipt:
        completed=json.loads(receipt.read_text())
        if (completed.get('returncode')!=0 or not completed.get('passed') or
                digest(build/'wa2_shared_aot_tests.exe')!=completed.get('binary_sha256') or
                digest(report)!=completed.get('report_sha256') or not json.loads(report.read_text()).get('passed')):
            raise ValueError('Independent differential result no longer matches this built test binary and report')
    else:run([build/'wa2_shared_aot_tests.exe',report],300)
    process_report=build/'function-process.json'
    run([build/'wa2_function_process_tests.exe',process_report],30)
    scheduler_report=build/'portable-scheduler.json'
    run([build/'wa2_portable_scheduler_tests.exe',scheduler_report],30)
    registry_report=build/'portable-registry.json'
    run([build/'wa2_portable_registry_tests.exe',registry_report],30)
    system_report=build/'portable-system.json'
    run([build/'wa2_portable_system_tests.exe',system_report],30)
    mount_report=build/'resource-mount.json'
    run([build/'wa2_resource_mount_tests.exe',mount_report],30)
    native_memory_report=build/'native-resource-memory.json'
    run([build/'wa2_native_resource_memory_tests.exe',native_memory_report],60)
    resource_loading_report=build/'resource-loading.json'
    run([build/'wa2_resource_loading_tests.exe',resource_loading_report],60)
    resource_loading=json.loads(resource_loading_report.read_text())
    if (not resource_loading['passed'] or resource_loading['exact_output_cases']!=64 or
        resource_loading['unchanged_failure_cases']!=22 or resource_loading['snapshot_alias_cases']!=8 or
        resource_loading['mapping_change_cases']!=2):raise ValueError('General resource decode output/failure contract failed')
    lzss_report=build/'lzss-groups.json'
    run([build/'wa2_lzss_group_tests.exe',lzss_report],30)
    lzss_groups=json.loads(lzss_report.read_text())
    if (not lzss_groups['passed'] or lzss_groups['exact_output_dictionary_flags_and_tail_cases']!=945 or
        lzss_groups['unchanged_failure_cases']!=260 or not lzss_groups['guard_bytes_checked'] or
        len(lzss_groups['timings'])!=6):raise ValueError('Grouped LZSS decode output/dictionary/flags/failure contract failed')
    typography={}
    for target,name in [('wa2_glyph_index_tests','glyph-index'),('wa2_glyph_geometry_tests','glyph-geometry'),('wa2_byte_equality_tests','byte-equality')]:
        result_path=build/(name+'.json');run([build/(target+'.exe'),result_path],30);typography[name]=json.loads(result_path.read_text())
        if not typography[name]['passed']:raise ValueError('Shared typography/byte comparison check failed: '+name)
    index=typography['glyph-index'];memo=typography['glyph-geometry'];equality=typography['byte-equality']
    if (index['full_codepoint_tables']!=26 or index['exact_codepoint_cases']!=1703936 or index['mutation_cases']!=256 or index['maximum_cache_bytes']>257*1024 or
        memo['key_mutations']!=932 or memo['exact_payload_cases']!=517 or memo['unchanged_declines']!=2 or memo['single_use_no_capture']!=512 or memo['maximum_heap_bytes']>256*1024 or
        not memo['owns_keys_and_tiles'] or equality['fixed_cases']!=9552 or equality['dynamic_cases']!=20488 or equality['word_cases']!=1282 or
        not equality['unaligned_and_exact_tails'] or not equality['outside_bytes_do_not_affect_result']):raise ValueError('Font index, exact geometry memo or bounded equality contract failed')
    wait_probe_report=build/'wait-probes.json'
    run([build/'wa2_wait_probe_tests.exe',wait_probe_report],30)
    wait_probes=json.loads(wait_probe_report.read_text())
    if (not wait_probes['passed'] or wait_probes['semantic_cases']!=13 or wait_probes['nonzero_sleep_ns']<8000000 or
        len(wait_probes['timings'])!=4 or any(v!=0 for item in wait_probes['timings'][:3] for v in item['heap_allocations']) or
        any(v!=512 for v in wait_probes['timings'][3]['heap_allocations'])):raise ValueError('Wait ordering/consumption/deadline/allocation contract failed')
    resource_cache_report=build/'native-resource-cache.json'
    run([build/'wa2_native_resource_cache_tests.exe',resource_cache_report],90)
    resource_cache=json.loads(resource_cache_report.read_text())
    if (not resource_cache['passed'] or resource_cache['exact_output_dictionary_and_trace_cases']!=44 or
        resource_cache['cache_boundary_cases']!=7 or resource_cache['caller_dictionary_change_cases']!=8 or
        resource_cache['unchanged_rejections']!=2 or resource_cache['native_cache_hits_checked']<22 or
        resource_cache['maximum_cache_bytes']!=8388608 or len(resource_cache['timing'])!=2 or
        resource_cache['maximum_idle_large_allocation_bytes']!=4194304 or len(resource_cache['large_allocation_timing'])!=2 or
        any(pair!=[4,0] for pair in resource_cache['large_allocation_timing'][1]['paired_native_commit_calls'])):
        raise ValueError('Bounded native image reuse/dictionary/identity validation failed')
    heap_report=build/'heap-pool-memory.json'
    run([build/'wa2_heap_pool_memory_tests.exe',heap_report],30)
    large_heap_report=build/'large-heap-memory.json'
    run([build/'wa2_large_heap_memory_tests.exe',large_heap_report],30)
    large_heap=json.loads(large_heap_report.read_text())
    if not large_heap['passed'] or large_heap['checks']!=44 or large_heap['reuse_cycles']!=32:raise ValueError('Large resource-buffer reuse contract failed')
    sound_report=build/'sound-bridge.json'
    run([build/'wa2_portable_sound_bridge_tests.exe',sound_report],30)
    window_report=build/'window-resources.json'
    run([build/'wa2_window_resource_tests.exe',window_report],30)
    window_resources=json.loads(window_report.read_text())
    if (not window_resources['passed'] or window_resources['cached_icon_controls']!=2 or
        window_resources['icon_size_selections']!=6 or window_resources['rejected_icon_inputs']<8000):raise ValueError('Cached version dialog icon resource contracts failed')
    widget_fixture=build/'widget-unicode-fixture.json';widget_report=build/'widget-unicode.json'
    run([build/'wa2_widget_unicode_tests.exe',widget_fixture],30)
    run([sys.executable,ROOT/'scripts/verify_widget_unicode.py',widget_fixture,widget_report],15)
    widget_unicode=json.loads(widget_report.read_text())
    if (not widget_unicode['passed'] or widget_unicode['unicode_input_cases']!=77 or
        widget_unicode['cached_menus']!=2 or widget_unicode['cached_dialogs']!=7 or widget_unicode['menu_items']!=67 or
        widget_unicode['controls']!=53 or widget_unicode['cached_icons']!=2 or widget_unicode['old_byte_quoting_failures_reproduced']!=3 or
        widget_unicode['utf16_code_units_covered']!=65536):raise ValueError('Native widget Unicode transport or command state changed')
    window_model_report=build/'window-model.json'
    run([build/'wa2_window_model_tests.exe',window_model_report],30)
    movie_report=build/'movie-bridge.json'
    run([build/'wa2_portable_movie_bridge_tests.exe',movie_report],30)
    movie_tracks_report=build/'movie-tracks.json'
    run([build/'wa2_movie_track_tests.exe',ROOT/'res/mv100.pak',movie_tracks_report],60)
    movie_tracks=json.loads(movie_tracks_report.read_text())
    if (not movie_tracks['passed'] or movie_tracks['audio_frames_to_eof']<=16 or movie_tracks['audio_cursor_bytes_read']<=8388608 or
        movie_tracks['exact_pcm_yuv_timestamp_prefixes']!=64 or movie_tracks['explicit_rejections']!=4 or
        movie_tracks['compressed_backlog_bytes'] or not movie_tracks['same_source_independent_positions'] or
        not movie_tracks['audio_eof_does_not_consume_video'] or movie_tracks['peak_video_frames']>4 or movie_tracks['peak_audio_frames']>16384):
        raise ValueError('Movie fixed-track streaming or full audio EOF/prefix/cancellation check failed')
    acm_report=build/'portable-acm.json'
    run([build/'wa2_portable_acm_tests.exe',acm_report],30)
    shell_report=build/'portable-shell.json'
    run([build/'wa2_portable_shell_tests.exe',shell_report],30)
    run([sys.executable,ROOT/'scripts/verify_android_java.py'],90)
    java_proof=json.loads((ROOT/'logs/android-java-verification.json').read_text())
    ownership=java_proof['widget_ownership']
    if (not ownership['passed'] or ownership['reopen_cycles']!=64 or ownership['activity_session_cases']!=9 or
        ownership['cached_dialogs']!=7 or ownership['cached_controls']!=53 or not ownership['invalid_snapshot_atomic'] or
        not ownership['stale_sessions_rejected']):raise ValueError('Android widget ownership/session memory contracts failed')
    widget_icons=java_proof['widget_icons']
    if (not widget_icons['passed'] or widget_icons['cached_icons']!=2 or widget_icons['exact_cached_pixels']!=2048 or
        widget_icons['style_cases']!=96 or widget_icons['rejected_inputs']<100 or not widget_icons['alpha_and_mask_checked']):raise ValueError('Android version dialog icon decoding failed')
    touch_gestures=java_proof['touch_gestures']
    if (not touch_gestures['passed'] or touch_gestures['gesture_traces']!=289 or touch_gestures['two_finger_taps']!=76 or touch_gestures['cancel_release_cases']!=69 or
        touch_gestures['control_source_traces']!=288 or not touch_gestures['disabled_long_press_checked'] or
        not touch_gestures['two_finger_taps_never_forward_clicks']):raise ValueError('Android touch/keyboard gesture input contracts failed')
    run([sys.executable,ROOT/'scripts/verify_sdl_jni.py'],60)
    run([acmake,'-S',ROOT/'tests/shared-aot-android','-B',android,'-G','Ninja',
         '-DCMAKE_MAKE_PROGRAM='+str(ROOT/'tools/android-sdk/cmake/3.22.1/bin/ninja.exe'),
         '-DCMAKE_TOOLCHAIN_FILE='+str(ndk/'build/cmake/android.toolchain.cmake'),
         '-DANDROID_ABI=arm64-v8a','-DANDROID_PLATFORM=android-26','-DANDROID_STL=c++_shared',
         '-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON','-DCMAKE_BUILD_TYPE=MinSizeRel','-DWA2_AOT_DIAGNOSTICS=OFF',
         '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON'])
    run([acmake,'--build',android,'--target','wa2_shared_aot_link_check','wa2_android','--parallel','1'],2400)
    library=android/'libwa2_shared_aot_link_check.so'
    reader=ndk/'toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-readelf.exe'
    elf=subprocess.run([str(reader),'--wide','-h','-l','-d',str(library)],cwd=ROOT,env=environment,
                       creationflags=flags,capture_output=True,text=True,check=True,timeout=10).stdout
    if 'AArch64' not in elf or '.dll' in elf.lower():raise ValueError('wrong architecture or Windows dependency')
    loads=[int(line.split()[-1],16) for line in elf.splitlines() if line.strip().startswith('LOAD ')]
    if not loads or any(x<16384 for x in loads):raise ValueError('ELF alignment is not 16 KiB compatible')
    stripped=android/'libwa2_shared_aot_link_check-stripped.so'
    run([reader.with_name('llvm-strip.exe'),'--strip-all','-o',stripped,library])
    symbols=subprocess.run([str(reader.with_name('llvm-nm.exe')),'--defined-only','--demangle',str(library)],
                           cwd=ROOT,env=environment,creationflags=flags,capture_output=True,text=True,check=True,timeout=10).stdout
    if re.search(r'wa2::aot::f_[0-9a-f]+<true(?:,|>)|f_[0-9a-f]+<false, false>|shared_aot_oracle|native_weather_color_snapshot|wr_x87_|wr_sse_|Zydis|NativeIntegerReferenceScope|native_integer_registers_enabled|NativeIndirectReferenceScope|native_indirect_calls_enabled|NativeMathReferenceScope|native_math_ports_enabled|native_math_test_counts|native_render_math_enabled|NativeRenderMathReferenceScope|native_grid_binary_snapshot|native_grid_stored_binary_snapshot|native_grid_registers_enabled|native_sprite_geometry_enabled|native_grid_float_enabled|native_vertex_grid_enabled|register_fp_enabled|RegisterFpReferenceScope|DecodedCacheReferenceScope|decoded_resource_cache_enabled|sort_objects<true>|vertex_batch<true>|wa2_sort_shift|wr::wa2_sort_objects\(|wr::wa2_vertex_batch\(',symbols):
        raise ValueError('diagnostic/oracle, decoder, or x86 helper leaked into the ARM64 ordinary build')
    if 'wa2_shared_aot_function_count' not in symbols:raise ValueError('shared ARM64 entry is not linked')
    if re.search(r'wa2::text::GlyphGeometryMemo<[^\n]+>::(?:stats|enabled)',symbols):
        raise ValueError('Glyph memo comparison controls leaked into the ARM64 ordinary build')
    if re.search(r'ImageColourReferenceScope|image_colours_enabled',symbols):
        raise ValueError('Image-colour comparison controls leaked into the ARM64 ordinary build')
    if re.search(r'GridNativeFloating<true, true, false, true(?:, [^>]+)?>::(?:scalar_get|scalar_set|scalar_push|execute_at|memory_at|binary)',symbols):
        raise ValueError('Scalar geometry helpers must be inlined into the compiled calculation')
    def compiled_sources(directory):
        commands=json.loads((directory/'compile_commands.json').read_text())
        return sorted(str(Path(command['file']).resolve()).lower() for command in commands
                      if any(target in command.get('command','') for target in
                             ('wa2_shared_aot.dir','wa2_function_process.dir','wa2_shared_audio.dir','wa2_aot_memory.dir',
                              'wa2_initial_data.dir','wa2_platform.dir','wa2_engine.dir','wa2_shared_graphics.dir',
                              'wa2_fixed_pipeline.dir','wa2_common_shader_parser.dir','wa2_common_shader_assembler.dir',
                              'wa2_common_shader_gl.dir','wa2_movie.dir','wa2_wave_codec.dir','wa2_pixel_transfer.dir','wa2_scalar_math.dir')))
    x64_sources,arm64_sources=compiled_sources(build),compiled_sources(android)
    if not x64_sources or x64_sources!=arm64_sources:
        raise ValueError('Windows and Android compiled different game AOT source sets')
    for directory in (build,android):
        commands=json.loads((directory/'compile_commands.json').read_text())
        for name in ('vertex_grid.cpp','native_weather.cpp','native_sprites.cpp'):
            native=[c['command'] for c in commands if Path(c['file']).name==name]
            if len(native)!=1 or any(flag not in native[0] for flag in ('-O2','-ffp-contract=off','-frounding-math')):
                raise ValueError('Native geometry/weather requires shared strict floating-point compiler options: '+name)
        audio=[c['command'] for c in commands if 'wa2_engine.dir' in c['command'] and Path(c['file']).name in ('pcm_audio.cpp','vorbis_stream.cpp')]
        if len(audio)!=2 or any(any(flag not in command for flag in ('-O3','-ffp-contract=off')) for command in audio):
            raise ValueError('Both hosts must compile the common audio cores with the same strict optimization options')
        resources=[c['command'] for c in commands if 'wa2_engine.dir' in c['command'] and Path(c['file']).name=='resource_archive.cpp']
        if len(resources)!=1 or '-O3' not in resources[0]:raise ValueError('Both hosts must optimize the same checked resource decoder')
        colours=[c['command'] for c in commands if 'wa2_shared_aot.dir' in c['command'] and Path(c['file']).name=='image_colours.cpp']
        if len(colours)!=1 or '-O3' not in colours[0]:raise ValueError('Both hosts must optimize the same native image-colour source')
    for name,value in bindings.items():
        if digest(ROOT/name)!=value:raise ValueError('source changed during verification: '+name)
    manifest=json.loads((ROOT/'generated/shared-aot/manifest.json').read_text())
    for name,value in manifest['source_sha256'].items():
        if digest(ROOT/name)!=value:raise ValueError('AOT discovery input changed: '+name)
    proof=dict(schema='wa2.shared-aot-verification.v1',source_sha256=bindings,
               manifest_sha256=digest(ROOT/'generated/shared-aot/manifest.json'),
               admitted_functions=manifest['admitted_functions'],instruction_locations=manifest['unique_instruction_locations'],
               differential=json.loads(report.read_text()),
               crt_math_paths=json.loads(crt_math_report.read_text()),
               native_pixel_transfer=json.loads(pixel_report.read_text()),
               shared_direct_image_upload=image_upload,
               shared_native_image_colours=image_colours,
               shared_bounded_movie_tracks=movie_tracks,
               shaders=shader_proof['checks'],
               shader_verification_sha256=digest(ROOT/'logs/portable-shader-verification.json'),
               function_runtime=json.loads(process_report.read_text()),
               portable_scheduler=json.loads(scheduler_report.read_text()),
               portable_registry=json.loads(registry_report.read_text()),
               portable_system=json.loads(system_report.read_text()),
               resource_mount=json.loads(mount_report.read_text()),
               native_resource_memory=json.loads(native_memory_report.read_text()),
               shared_general_resource_loading=resource_loading,
               shared_lzss_groups=lzss_groups,
               shared_typography=typography,
               shared_wait_probes=wait_probes,
               native_resource_cache=json.loads(resource_cache_report.read_text()),
               shared_heap_pool_memory=json.loads(heap_report.read_text()),
               shared_large_heap_memory=json.loads(large_heap_report.read_text()),
               shared_sound_bridge=json.loads(sound_report.read_text()),
               window_resources=json.loads(window_report.read_text()),
               shared_widget_unicode=widget_unicode,
               shared_widget_ownership=ownership,
               shared_widget_icons=widget_icons,
               shared_touch_gestures=touch_gestures,
               shared_main_story_subtitles=subtitle_checks,
               window_model=json.loads(window_model_report.read_text()),
               render_submission=json.loads(submission_report.read_text()),
               shared_host_leaf_calls=json.loads(leaf_report.read_text()),
               native_integer_registers=json.loads(integer_report.read_text()),
               native_bulk_string_work=json.loads(string_report.read_text()),
               shared_large_native_buffers=buffers,
               native_interface_handle_memory=json.loads(interface_report.read_text()),
               bounded_particle_audio_comparison=json.loads(particle_audio_report.read_text()),
               bounded_particle_comparison_to_previous_release=json.loads(particle_report.read_text()),
               native_weather_compilation=json.loads((ROOT/'generated/shared-aot/weather_batch.json').read_text()),
               shared_render_cpu_snapshots=json.loads(render_cpu_report.read_text()),
               shared_render_cpu_24bit=json.loads(render_cpu_24_report.read_text()),
               shared_render_cpu_dense_grid=json.loads(render_dense_report.read_text()),
               shared_portrait_texture_batch=json.loads(portrait_report.read_text()),
               shared_dialogue_geometry=json.loads(dialogue_report.read_text()),
               shared_native_sprite_drawing=json.loads(sprite_report.read_text()),
               shared_particle_draw_pipeline=json.loads(pipeline_report.read_text()),
               shared_render_cpu_gradient=json.loads(render_varied_report.read_text()),
               shared_render_cpu_masked=json.loads(render_masked_report.read_text()),
               shared_render_cpu_serialized_reference=json.loads(render_serialized_report.read_text()),
               portable_fp_kernel=json.loads((ROOT/'logs/fp-kernel-tests.json').read_text()),
               portable_simd_kernel=json.loads((ROOT/'logs/simd-kernel-tests.json').read_text()),
               portable_fp_execution=json.loads((ROOT/'logs/fp-execution-tests.json').read_text()),
               portable_fp_workload=json.loads((ROOT/'logs/fp-workload-tests.json').read_text()),
               movie_bridge=json.loads(movie_report.read_text()),
               wave_acm=json.loads(acm_report.read_text()),
               shell=json.loads(shell_report.read_text()),
               partially_admitted_entries=manifest['partially_admitted_entries'],
               unsupported_instruction_sites=manifest['unsupported_instruction_sites'],
               actual_android_host=dict(file=str(android/'actual_host/libwa2_android.so'),
                    sha256=digest(android/'actual_host/libwa2_android.so'),
                    bytes=(android/'actual_host/libwa2_android.so').stat().st_size),
               windows_library=dict(file=str(build/'libwa2_shared_aot.a'),sha256=digest(build/'libwa2_shared_aot.a'),bytes=(build/'libwa2_shared_aot.a').stat().st_size),
               android_library=dict(file=str(library),sha256=digest(library),bytes=library.stat().st_size,load_alignments=loads,
                                    stripped_file=str(stripped),stripped_bytes=stripped.stat().st_size,stripped_sha256=digest(stripped)),
               actual_compiled_shared_sources=x64_sources,
               same_generated_sources=True,arm64_link_verified=True,android_device_verified=False,
               diagnostic_instruction_bodies_in_android_library=False,
               actual_arm64_symbols_checked=True,oracle_linked_in_android_library=False,
               complete_gameplay_verified=False,source_executable_opened=False,screen_access=False,
               graphics_device_created=False,gpu_shader_compile_verified=False,
               game_launched=False,emulator_launched=False,maximum_build_jobs=1,passed=True)
    (ROOT/'logs/shared-aot-verification.json').write_text(json.dumps(proof,indent=2)+'\n')
    print('PASS one static function source set, x64 state differences and full-object ARM64 link; no whole-game or device claim.')


if __name__=='__main__':
    main()
