"""Refresh unchanged verified static sources after host/storage/pacing changes.

The original EXE and deleted oracle/baseline objects are never opened or
regenerated. This records its narrower checks explicitly rather than claiming
the historical differential/performance suite ran. One hidden low-priority job;
only finite own-memory/file fixtures execute. Both actual hosts are linked.
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
from verify_shared_package import common_sources
from verify_subtitles import verify as verify_subtitles
from prepare_shared_chroma import prepare as prepare_chroma
from prepare_shared_language import prepare as prepare_language
from prepare_shared_progress import prepare as prepare_progress

ROOT=Path(__file__).resolve().parents[1]
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def fresh(path):
    report=json.loads(path.read_text())
    if not report.get('passed'):raise ValueError('A passing report is required: '+str(path))
    for name,value in report['source_sha256'].items():
        if digest(ROOT/name)!=value:raise ValueError('Changed verification input: '+name)
    return report

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference',type=Path,required=True,help='Previous passing AOT verification receipt retained outside public source')
    parser.add_argument('--windows-sdl',type=Path,required=True,help='Existing SDL archive matching that receipt')
    parser.add_argument('--zig-cache',type=Path,required=True)
    parser.add_argument('--check-inputs-only',action='store_true')
    args=parser.parse_args()
    kernel=ctypes.WinDLL('kernel32',use_last_error=True);kernel.GetCurrentProcess.restype=ctypes.c_void_p
    kernel.SetPriorityClass.argtypes=[ctypes.c_void_p,ctypes.c_uint];kernel.SetProcessAffinityMask.argtypes=[ctypes.c_void_p,ctypes.c_size_t]
    kernel.GetProcessAffinityMask.argtypes=[ctypes.c_void_p,ctypes.POINTER(ctypes.c_size_t),ctypes.POINTER(ctypes.c_size_t)]
    process=kernel.GetCurrentProcess();current=ctypes.c_size_t();system=ctypes.c_size_t()
    if not kernel.SetPriorityClass(process,0x4000) or not kernel.GetProcessAffinityMask(process,ctypes.byref(current),ctypes.byref(system)) or not kernel.SetProcessAffinityMask(process,current.value&-current.value):raise ctypes.WinError()
    flags=subprocess.CREATE_NO_WINDOW|subprocess.BELOW_NORMAL_PRIORITY_CLASS
    reference_path=args.reference.resolve();reference=json.loads(reference_path.read_text())
    if not reference.get('passed') or reference.get('schema')!='wa2.shared-aot-verification.v1':raise ValueError('A previous full AOT verification receipt is required')
    if not reference_path.is_relative_to(ROOT):raise ValueError('Keep the private reference receipt within this workspace')
    generated={}
    for folder in ('generated/shared-aot','generated/static-data','generated/codepages','generated/graphics-abi'):
        for path in (ROOT/folder).iterdir():
            if not path.is_file():continue
            name=path.relative_to(ROOT).as_posix();value=digest(path)
            if reference['source_sha256'].get(name)!=value:raise ValueError('Unverified or changed cached generated input: '+name)
            generated[name]=value
    if len(generated)<20:raise ValueError('Incomplete cached static source set')
    chroma=prepare_chroma()
    language_sources=prepare_language()
    progress_sources=prepare_progress()
    archive=args.windows_sdl.resolve();sdl_hash=digest(archive)
    if sdl_hash not in {value for name,value in reference['source_sha256'].items() if name.endswith('/libSDL2.a')}:raise ValueError('Windows SDL archive differs from the verified reference')
    unchanged_math={name:value for name,value in reference['source_sha256'].items() if name.startswith('recompiled/') and Path(name).name in ('native_math.cpp','native_math.hpp','native_gateways.cpp','extended80.cpp','extended80.hpp','transcend.cpp')}
    for name,value in unchanged_math.items():
        if digest(ROOT/name)!=value:raise ValueError('Math source changed; run its independent differential verification: '+name)
    if len(unchanged_math)<5:raise ValueError('Missing verified native math source provenance')
    if args.check_inputs_only:
        print('Verified '+str(len(generated))+' unchanged cached generated inputs and the reference SDL archive; no build or game started.');return
    build=ROOT/'build/shared-aot-refresh-x64';android=ROOT/'build/shared-aot-android';sdl_build=ROOT/'build/android-native-game'
    env=os.environ.copy();env.update(CMAKE_BUILD_PARALLEL_LEVEL='1',ZIG_GLOBAL_CACHE_DIR=str(args.zig_cache.resolve()),ZIG_LOCAL_CACHE_DIR=str(build/'zig-local'))
    for name in ('TEMP','TMP'):
        directory=ROOT/'build/native-core/temp';directory.mkdir(parents=True,exist_ok=True);env[name]=str(directory)
    cmake=ROOT/'tools/python/cmake/data/bin/cmake.exe';ninja=ROOT/'tools/python/bin/ninja.exe'
    ndk=ROOT/'tools/android-sdk/ndk/27.0.12077973';llvm=ndk/'toolchains/llvm/prebuilt/windows-x86_64/bin'
    with (ROOT/'logs/shared-aot-refresh-build.log').open('wb') as log:
        def run(command,timeout=2400):
            command=list(map(str,command));log.write(('RUN '+subprocess.list2cmdline(command)+'\n').encode());log.flush()
            with subprocess.Popen(command,cwd=ROOT,env=env,creationflags=flags,stdout=log,stderr=subprocess.STDOUT) as child:
                try:status=child.wait(timeout)
                except subprocess.TimeoutExpired:
                    subprocess.run(['taskkill','/PID',str(child.pid),'/T','/F'],creationflags=flags,stdout=log,stderr=subprocess.STDOUT,timeout=20);child.wait();raise
                if status:raise subprocess.CalledProcessError(status,command)
        def output(command):return subprocess.run(list(map(str,command)),cwd=ROOT,env=env,creationflags=flags,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=120,check=True).stdout
        ui=fresh(ROOT/'logs/native-android-ui.json')
        for source_set in ui['source_sets'].values():
            for name,value in source_set['classes_sha256'].items():
                if digest(Path(source_set['classes_directory'])/name)!=value:raise ValueError('Compiled Android presentation class changed: '+name)
        movies=fresh(ROOT/'logs/movie-codec-verification.json')
        if movies['patch_movies']!=16 or movies['baseline_movies']!=2 or movies['full_audio_files']!=18 or movies['full_video_files']!=4 or not all(movies[key] for key in ('same_shared_reader','original_resources_external','unknown_codecs_rejected')):raise ValueError('Incomplete original/Final Full Patch movie checks')
        movie_formats=fresh(ROOT/'logs/movie-format-verification.json')
        if not all(movie_formats[key] for key in ('bad_movie_completes_source_wait','unknown_guest_faults_preserved','same_shared_reader')) or movie_formats['formats']['recoveries']!=2 or movie_formats['source_completion']['parse_decode_failure_cases']!=16:raise ValueError('Incomplete common-format and movie-skip checks')
        media_builds={}
        media_policy=json.loads((ROOT/'config/media_codecs.json').read_text())
        for host in ('windows','arm64'):
            media=json.loads((ROOT/('logs/media-codec-'+host+'-build.json')).read_text())
            if media['movie_decoders']!=media_policy['video_decoders']+media_policy['audio_decoders'] or media['movie_demuxers']!=media_policy['demuxers'] or media['media_policy_sha256']!=digest(ROOT/'config/media_codecs.json') or not media['thread_safe_initialization'] or media['decoder_thread_count_policy']!=1:raise ValueError('Incomplete movie decoder build: '+host)
            for name,value in media['libraries'].items():
                if digest(ROOT/'tools/media-codec'/host/name)!=value['sha256']:raise ValueError('Changed movie decoder library: '+host+'/'+name)
            media_builds[host]=media
        try:shaders=fresh(ROOT/'logs/portable-shader-verification.json')
        except (OSError,ValueError):
            run([sys.executable,ROOT/'scripts/verify_portable_shaders.py']);shaders=fresh(ROOT/'logs/portable-shader-verification.json')
        tracked=[Path(__file__),ROOT/'tests/shared-host-refresh/CMakeLists.txt',reference_path,archive,ROOT/'config/porting_contract.json',ROOT/'config/release.json']
        tracked += [ROOT/'tests'/f'{name}.cpp' for name in ('selected_save_files','portable_system','resource_mount','test_profile','portable_scheduler','function_process','subtitles','portable_shell','fast_forward','script_wait_callback','save_inventory','profile_batch','io_diagnostics','resource_reuse','resource_loading','loose_resources','pc_loose_resources','host_progress','image_chroma','language_resources','language_names','language_prompts','ukrainian','external_subtitles','music_flag_saves','subtitle_spacing')]
        tracked += [ROOT/'scripts/prepare_shared_chroma.py',ROOT/chroma['reference_file']]
        tracked += [ROOT/'scripts/prepare_shared_language.py',ROOT/'scripts/prepare_ukrainian_resources.py',ROOT/'scripts/pack_lzss.cpp']
        tracked += [ROOT/'scripts/prepare_shared_progress.py']
        tracked += [ROOT/'scripts/read_android_counters.py',ROOT/'tests/android_counters.py']
        tracked += [ROOT/'scripts/build_media_codec.py',ROOT/'scripts/verify_movie_codecs.py',ROOT/'tests/movie_codecs.cpp',ROOT/'logs/movie-codec-verification.json']
        tracked += [ROOT/'scripts/verify_movie_formats.py',ROOT/'scripts/make_movie_format_samples.py',ROOT/'tests/movie_formats.cpp',ROOT/'tests/portable_movie_bridge.cpp',ROOT/'logs/movie-format-verification.json']
        tracked += [ROOT/('logs/media-codec-'+host+'-build.json') for host in ('windows','arm64')]
        tracked += [ROOT/'scripts/verify_subtitles.py',ROOT/'scripts/verify_shared_package.py',ROOT/'scripts/zig-toolchain.cmake',ROOT/'tests/shared-aot-android/CMakeLists.txt',ROOT/'tests/shared-aot-android/entry.cpp']
        for folder in ('recompiled','engine','platform','hosts','android/native','generated/shared-aot','generated/static-data','generated/codepages','generated/graphics-abi','third_party/wa2_subtitles','tools/WindowsRecompTool-main/include/winrecomp'):
            tracked += [path for path in (ROOT/folder).rglob('*') if path.is_file() and path.suffix in ('.cpp','.hpp','.h','.inc','.cmake','.txt','.json')]
        tracked += [ROOT/('tools/WindowsRecompTool-main/src/'+name) for name in ('runtime/native_memory.cpp','runtime/process.cpp','runtime/win32.cpp','pe.cpp','resources.cpp')]
        tracked += [ROOT/('port/'+name) for name in ('audio.cpp','audio.hpp','audio_abi.hpp','audio_tables.inc','native_resources.cpp','native_resources.hpp','native_vorbis.cpp','native_vorbis.hpp','registry.cpp','wa2_win32.hpp','exceptions.cpp','exception_abi.hpp','sprite_sort.cpp','sprite_sort.hpp','sort_signature.inc','vertex_batch.cpp','vertex_batch.hpp','vertex_signature.inc','d3d9_tables.inc')]
        tracked += [ROOT/'config/media_codecs.json']
        tracked += [ROOT/('tools/media-codec/'+host+'/lib/'+name) for host in ('windows','arm64') for name in ('libavformat.a','libavcodec.a','libswscale.a','libswresample.a','libavutil.a')]
        bindings={path.relative_to(ROOT).as_posix():digest(path) for path in tracked};bindings.update(ui['source_sha256']);bindings.update(shaders['source_sha256'])
        print('Building verified cached AOT sources and selected-directory checks on x64; one hidden low-priority job.',flush=True)
        run([cmake,'-S',ROOT/'tests/shared-host-refresh','-B',build,'-G','Ninja','-DCMAKE_MAKE_PROGRAM='+str(ninja),'-DCMAKE_TOOLCHAIN_FILE='+str(ROOT/'scripts/zig-toolchain.cmake'),'-DCMAKE_BUILD_TYPE=MinSizeRel','-DCMAKE_EXPORT_COMPILE_COMMANDS=ON','-DWA2_SHARED_SDL_ARCHIVE='+str(archive)])
        tests={name:'wa2_refresh_'+name for name in ('selected_save_files','portable_system','resource_mount','test_profile','portable_scheduler','function_process','subtitles','portable_shell','fast_forward','script_wait_callback','save_inventory','profile_batch','io_diagnostics','resource_reuse','resource_loading','loose_resources','pc_loose_resources','host_progress','image_chroma','language_resources','language_names','language_prompts','ukrainian','external_subtitles','music_flag_saves','subtitle_spacing')}
        run([cmake,'--build',build,'--parallel','1','--target',*tests.values()])
        reports={}
        for name,target in tests.items():
            path=build/(name+'.json');run([build/(target+'.exe'),path],120);report=json.loads(path.read_text())
            if not report.get('passed') or report.get('game_launched') or report.get('screen_access') or report.get('source_executable_opened'):raise ValueError('Finite memory/file test failed: '+name)
            reports[name]=report
        reader_path=build/'counter_reader.json';run([sys.executable,ROOT/'tests/android_counters.py',reader_path]);reports['counter_reader']=json.loads(reader_path.read_text())
        if not reports['counter_reader']['passed'] or reports['counter_reader']['parser_cases']<12:raise ValueError('Incomplete bounded counter assembly checks')
        language=reports['language_resources']
        music=reports['music_flag_saves']
        if music['baseline_repeated_opens']!=480 or music['optimized_repeated_opens']!=4 or music['guards']<48 or music['plugin_notifications']!=music['actual_static_setter_calls'] or not all(music[k] for k in ('same_final_flags_and_nonflag_save_bytes','failed_open_short_write_and_close_retry','other_callers_workers_and_namespaces_preserved')):raise ValueError('Incomplete idempotent title music unlock checks')
        external=reports['external_subtitles']
        spacing=reports['subtitle_spacing']
        if spacing['unchanged_chinese_cues']!=315 or spacing['layout_cases']<13 or spacing['guards']<11 or spacing['native_font_metric_cases']!=3 or not all(spacing[k] for k in ('font_size_unchanged','word_wrap_and_explicit_newlines','zero_advance_marks_checked','yaml_and_todokanai_checked','playback_layout_used')):raise ValueError('Incomplete proportional external subtitle layout checks')
        if external['exact_embedded_cues']!=315 or external['exact_embedded_glyphs']!=6462 or external['rejections']<45 or not all(external[key] for key in ('bom_crlf_checked','shared_audio_binding_checked','external_replaces_embedded_tracks','new_character_atlas_checked','legacy_bitmap_reused','bounded_yaml_only')):raise ValueError('Incomplete external sub.yaml parser/playback checks')
        if language['archive_aliases']!=12 or language['shared_argument_combinations']!=6 or language['guards']<31 or not all(language[key] for key in ('default_chinese_pair','automatic_matched_pairs','root_ic_and_file_apis_checked','patch_priority_preserved','invalid_selection_not_published','english_grouped_override_and_base_script_fallback','english_font_metadata_guard')):raise ValueError('Incomplete selected language resource checks')
        if language['english_native_resource_cases']!=28:raise ValueError('Incomplete English lookup/decode callback checks in both memory modes')
        names=reports['language_names']
        if names['changed_slots']!=20 or names['preserved_slots']!=12 or names['guards']<9 or names['static_function_entries'] or names['instruction_step_calls'] or not all(names[key] for key in ('default_chinese_unchanged','source_strings_unchanged','glyph_tables_and_code_unchanged','alias_identity_preserved','invalid_contract_not_published')):raise ValueError('Incomplete language-specific default-name checks')
        prompts=reports['language_prompts']
        if prompts['prompts_per_language']!=7 or prompts['language_memory_cases']!=6 or prompts['localized_prompt_cases']!=28 or prompts['glyph_table_cases']<600 or prompts['guards']<34 or prompts['static_function_entries'] or prompts['instruction_step_calls'] or not all(prompts[key] for key in ('default_chinese_unchanged','japanese_source_text','english_ascii_prompts','adjacent_data_and_code_unchanged','read_only_permissions_preserved','all_contracts_checked_before_replacement')):raise ValueError('Incomplete language-specific save/load menu prompt checks')
        host=reports['host_progress']
        ukrainian=reports['ukrainian']
        if ukrainian['resource_callback_cases']!=18 or ukrainian['text_policy_cases']!=1344 or ukrainian['guards']<13 or not all(ukrainian[k] for k in ('main_ic_separate','matched_font_and_names','localized_prompts','source_code_immutable','legacy_subtitle_audio_binding','legacy_newlines_and_overlaps')):raise ValueError('Incomplete shared Ukrainian/Todokanai checks')
        if host['burst_queries']!=3072 or host['device_pumps']!=12 or host['transport_roundtrips']<4 or not host['blocking_waits_bypass'] or not host['frame_summary_exact']:raise ValueError('Incomplete event cadence/progress checks')
        image_chroma=reports['image_chroma']
        if image_chroma['exact_cpu_flags_stack_and_pixel_cases']!=756 or image_chroma['admitted_cases']!=360 or image_chroma['loop_exit_state_cases']!=36 or image_chroma['precise_fault_cases']<40 or image_chroma['partial_region_tail_cases']!=6 or not image_chroma['cached_reference_bodies_unchanged'] or not image_chroma['direct_linear_path_exercised']:raise ValueError('Incomplete cached-source chroma comparisons')
        saves=reports['selected_save_files']
        inventory=reports['save_inventory']
        if inventory['actual_static_index_calls']<1600 or not all(inventory[key] for key in ('chapter_index_batch','chapter_namespace_separate','missing_chapter_queries_batched')):raise ValueError('Incomplete initial chapter-index checks')
        if inventory.get('full_bank_slots')!=100 or not inventory.get('full_bank_single_inventory') or not inventory.get('root_previews_ignored'):raise ValueError('Missing full 100-slot canonical save-bank check')
        if inventory.get('read_ahead_index_calls',0)<100 or inventory.get('full_bank_read_ahead_batches')!=7 or inventory.get('maximum_read_ahead_files')!=16 or not all(inventory.get(k) for k in ('read_ahead_handles_released','read_ahead_failure_retries','read_ahead_writers_and_workers_bypass')):raise ValueError('Missing bounded save read-ahead ownership/guard checks')
        if not inventory.get('read_ahead_only_manual_slots') or not inventory.get('read_ahead_single_file_bypass'):raise ValueError('Read ahead includes unrelated files or single-file batches')
        if inventory.get('repeat_list_rounds')!=20 or inventory.get('repeat_list_directory_queries')!=1 or inventory.get('repeat_list_calls',0)<80 or inventory.get('list_metadata_maximum_age_ms')!=2000 or not all(inventory.get(k) for k in ('list_payload_reads_fresh','full_read_new_files_immediate','list_expiry_and_mutation_checked','list_cached_openers_hold_no_descriptors')):raise ValueError('Incomplete bounded repeated save-list metadata checks')
        if inventory.get('page_preview_calls',0)<50 or inventory.get('page_preview_guards',0)<30 or inventory.get('page_preview_real_stack_checks',0)<50 or inventory.get('maximum_page_preview_files')!=10 or inventory.get('page_preview_batches_per_full_page')!=1 or not all(inventory.get(k) for k in ('page_preview_boundaries_checked','page_preview_confirmed_loads_excluded','page_preview_sparse_and_failures_checked','page_preview_mode_transition_checked','page_preview_no_ui_executed')):raise ValueError('Incomplete current-page preview read-ahead checks')
        if not inventory.get('page_preview_slow_update_checked') or inventory.get('maximum_preview_scope_ms')!=1000:raise ValueError('Missing bounded slow preview update check')
        progress=reports['io_diagnostics']
        if progress['selected_directory_written'] or not all(progress[key] for key in ('atomic_replacement','unchanged_io_not_rewritten','resource_activity_sampled','rate_and_count_bounded','failed_write_retry','optional_sink_failure_isolated','guest_errors_preserved')):raise ValueError('Incomplete bounded I/O diagnostic checks')
        reuse=reports['resource_reuse']
        if reuse['maximum_cache_bytes']!=8388608 or reuse['exact_output_dictionary_trace_cases']<18 or reuse['large_image_reuses']<2 or reuse['archive_invalidations']!=2 or reuse['caller_tail_changes']!=4 or not reuse['total_budget_unchanged']:raise ValueError('Incomplete resource cache admission checks')
        loading=reports['resource_loading']
        if loading['exact_output_cases']<60 or loading['unchanged_failure_cases']<20 or not loading['paged_and_contiguous_memory']:raise ValueError('Incomplete general resource loading checks')
        profiles=reports['profile_batch']
        pc_loose=reports['pc_loose_resources']
        if pc_loose['static_reader_calls']!=52 or pc_loose['profiles']!=4 or pc_loose['guards']<12 or not all(pc_loose[k] for k in ('pc_script_and_font_priority','main_ic_separate','partial_font_patch_falls_back_to_en_group','legacy_loose_folders_preserved','prepared_sidecars_not_required','private_resource_fallback_forbidden')):raise ValueError('Incomplete PC loose script/font fallback checks')
        loose=reports['loose_resources']
        if loose['static_resource_reader_calls']<24 or loose['guard_cases']<40 or loose['resource_group_cases']!=10 or loose['encoded_filename_cases']!=8 or loose['static_resource_check_calls']<4 or not all(loose[k] for k in ('external_before_patch_and_pak','missing_external_falls_back','ic_and_case_insensitive_paths','fresh_stream_contents','name_cache_expiry','resource_namespace_read_only','source_optional_backup_not_fatal','private_resource_fallback_forbidden','canonical_saves_preserved')):raise ValueError('Incomplete loose resource/source reader checks')
        if reports['language_resources']['loose_language_alias_cases']<20 or not reports['language_resources']['loose_script_and_font_language_isolation']:raise ValueError('Incomplete language-specific loose resource alias checks')
        if reports['ukrainian']['loose_ukrainian_alias_cases']<10:raise ValueError('Incomplete Ukrainian loose resource alias checks')
        if profiles['static_profile_getter_calls']<400 or profiles['batched_descriptor_opens']!=1 or not all(profiles[key] for key in ('same_static_getter','writes_immediate','flush_and_revision_invalidation','frame_and_age_bounded','ordinary_and_worker_reads_fresh','namespace_isolation','failed_read_retry')):raise ValueError('Incomplete external profile read batch checks')
        if profiles['static_profile_setter_calls']<400 or profiles['setter_real_stack_checks']<300 or profiles['setter_guard_cases']<30 or profiles['baseline_exit_profile_loads']!=53 or profiles['optimized_exit_profile_loads']!=1 or profiles['baseline_unchanged_exit_commits']!=53 or profiles['optimized_unchanged_exit_commits']!=0 or not all(profiles[key] for key in ('changed_writes_still_immediate','ordinary_profile_writes_unchanged','write_scope_failure_age_and_namespace_guards','source_exit_and_ui_not_executed')):raise ValueError('Incomplete scoped source profile write checks')
        if inventory['actual_static_reader_calls']<800 or inventory['scoped_directory_lists_per_fixture']!=1 or not all(inventory[key] for key in ('same_static_save_reader','prepared_reads_checked','writers_bypass_read_reference','title_reader_batch','scope_audit_checked','payload_reads_fresh','write_invalidation','source_frame_invalidation','ordinary_and_worker_reads_fresh','inventory_lifetime_bounded')):raise ValueError('Incomplete source save inventory checks')
        if not all(saves[key] for key in ('single_lookup_open_plan','next_operation_sees_external_edits','sharing_checked_before_truncation')) or saves['fixture_main_opens']!=saves['fixture_main_metadata_queries']:raise ValueError('Repeated or stale save-open metadata queries')
        skip=reports['fast_forward']
        if skip['static_predicate_comparisons']<7000 or not all(skip[key] for key in ('unchanged_static_predicate','unread_policy_preserved','worker_and_object_waits_preserved','game_clock_unscaled','source_sync_restored','presentation_only_coalescing')):raise ValueError('Incomplete fast-forward scheduling checks')
        if saves['cases']<40 or not all(saves[key] for key in ('canonical_pc_files_read','source_path_aliases','alias_sharing_preserved','random_access_writes','profile_roundtrip','canonical_pc_only','test_profile_isolated','private_fallback_forbidden')):raise ValueError('Incomplete selected-directory file checks')
        if not all(saves.get(key) for key in ('root_save_fallback_forbidden','all_save_operations_canonical','missing_parent_created','selected_roots_isolated')):raise ValueError('Missing canonical-only save-bank checks')
        subtitle=verify_subtitles(build/'subtitles.json',ROOT/'generated/shared-aot/manifest.json')
        print('Building the same AOT sources and actual Android entry on ARM64; no application starts.',flush=True)
        android_args=['-G','Ninja','-DCMAKE_MAKE_PROGRAM='+str(ninja),'-DCMAKE_TOOLCHAIN_FILE='+str(ndk/'build/cmake/android.toolchain.cmake'),'-DANDROID_ABI=arm64-v8a','-DANDROID_PLATFORM=android-26','-DANDROID_STL=c++_shared','-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON','-DCMAKE_BUILD_TYPE=MinSizeRel','-DWA2_AOT_DIAGNOSTICS=OFF','-DCMAKE_EXPORT_COMPILE_COMMANDS=ON']
        run([cmake,'-S',ROOT/'android','-B',sdl_build,*android_args]);run([cmake,'--build',sdl_build,'--target','SDL2','--parallel','1'])
        run([cmake,'-S',ROOT/'tests/shared-aot-android','-B',android,*android_args]);run([cmake,'--build',android,'--target','wa2_shared_aot_link_check','wa2_android','--parallel','1'])
        x64_sources,arm64_sources=common_sources(build),common_sources(android)
        if not x64_sources or x64_sources!=arm64_sources:raise ValueError('Different common game sources on x64 and ARM64')
        actual=android/'actual_host/libwa2_android.so';header=output([llvm/'llvm-readelf.exe','--wide','-h','-l','-d',actual])
        if 'AArch64' not in header or '.dll' in header.lower():raise ValueError('Wrong actual Android architecture/dependency')
        alignment=[int(line.split()[-1],16) for line in header.splitlines() if line.strip().startswith('LOAD ')]
        if not alignment or min(alignment)<16384:raise ValueError('Android host lacks 16 KiB alignment')
        symbols=output([llvm/'llvm-nm.exe','--defined-only','--demangle',actual])
        if re.search(r'Zydis|shared_aot_oracle|wr_x87_|wr_sse_|wa2::game::|wa2::script::|wa2::aot::chroma_reference::|wa2::aot::f_[0-9a-f]+<true>',symbols):raise ValueError('Decoder, diagnostic or separate native game leaked into the AOT application')
        data=actual.read_bytes()
        expected={'compatibleOpen':'(Ljava/lang/String;III)I','compatibleReadMetadata':'(Ljava/lang/String;IZ)[Ljava/lang/String;','compatibleReadOpen':'(Ljava/lang/String;Ljava/lang/String;I)I','compatibleReadBatch':'([Ljava/lang/String;[Ljava/lang/String;I)[I','compatibleOperation':'(Ljava/lang/String;Ljava/lang/String;III)I','compatibleReplace':'(Ljava/lang/String;ILjava/nio/ByteBuffer;)V'}
        declarations=output([ROOT/'tools/source-analysis/jdk-21.0.12.1+1/bin/javap.exe','-p','-s',Path(ui['source_sets']['shared-aot']['classes_directory'])/'org/wa2/port/ResourceDirectory.class'])
        for name,signature in expected.items():
            if not re.search(r'\b'+name+r'\([^;]*;\s*descriptor:\s*'+re.escape(signature),declarations) or name.encode()+b'\0' not in data or signature.encode()+b'\0' not in data:raise ValueError('Actual selected-directory Java/JNI signature mismatch: '+name)
        subtitle_declarations=output([ROOT/'tools/source-analysis/jdk-21.0.12.1+1/bin/javap.exe','-p','-s',Path(ui['source_sets']['shared-aot']['classes_directory'])/'org/wa2/port/ExternalSubtitles.class'])
        for name,signature in {'readForGame':'()[B','rasterize':'([I)[I','nativeValidate':'([B)Ljava/lang/String;'}.items():
            if not re.search(r'\b'+name+r'\([^;]*;\s*descriptor:\s*'+re.escape(signature),subtitle_declarations):raise ValueError('External subtitle Java signature changed: '+name)
        if 'Java_org_wa2_port_ExternalSubtitles_nativeValidate' not in symbols or b'org/wa2/port/ExternalSubtitles\0' not in data or b'([I)[I\0' not in data or b'()[B\0' not in data:raise ValueError('External subtitle parser/font JNI bridge missing from actual ARM64 host')
        if b'LandscapeLeft LandscapeRight\0' not in data:raise ValueError('Actual AOT entry lacks landscape-only orientation')
        for name,value in bindings.items():
            if digest(ROOT/name)!=value:raise ValueError('Source changed during refresh: '+name)
        manifest=json.loads((ROOT/'generated/shared-aot/manifest.json').read_text());sdl=sdl_build/'sdl2/libSDL2.so'
        bindings[sdl.relative_to(ROOT).as_posix()]=digest(sdl)
        proof=dict(schema='wa2.shared-aot-refresh.v1',source_sha256=bindings,reference_receipt=str(reference_path),reference_receipt_sha256=digest(reference_path),cached_generated_sha256=generated,
            verification_scope='unchanged verified AOT sources, fresh unmetered shared-host builds, selected memory/file and fast-forward scheduling tests, Java/JNI audit',historical_oracle_and_performance_suite_rerun=False,
            current_tests=reports,movie_codec_verification=movies,movie_format_verification=movie_formats,media_codec_builds=media_builds,derived_chroma_sources=chroma,derived_language_sources=language_sources,derived_progress_sources=progress_sources,selected_directory_files=saves,save_inventory=inventory,profile_batches=profiles,fast_forward_scheduling=skip,shared_main_story_subtitles=subtitle,chapter_test_profile=reports['test_profile'],shaders=shaders['checks'],
            crt_math_paths=dict(passed=True,source_unchanged=True,reference_receipt_sha256=digest(reference_path),independent_math_test_rerun=False,source_sha256=unchanged_math),
            manifest_sha256=digest(ROOT/'generated/shared-aot/manifest.json'),admitted_functions=manifest['admitted_functions'],instruction_locations=manifest['unique_instruction_locations'],
            partially_admitted_entries=manifest['partially_admitted_entries'],unsupported_instruction_sites=manifest['unsupported_instruction_sites'],
            actual_android_host=dict(file=str(actual),sha256=digest(actual),bytes=actual.stat().st_size),actual_compiled_shared_sources=x64_sources,
            windows_sdl_archive=dict(file=str(archive),sha256=sdl_hash),selected_directory_jni_signatures=expected,
            windows_library=dict(file=str(build/'shared_aot/libwa2_shared_aot.a'),sha256=digest(build/'shared_aot/libwa2_shared_aot.a')),
            same_generated_sources=True,arm64_link_verified=True,game_launched=False,screen_access=False,android_device_verified=False,complete_gameplay_verified=False,passed=True)
        (ROOT/'logs/shared-aot-verification.json').write_text(json.dumps(proof,indent=2)+'\n',encoding='utf-8')
        print('PASS unchanged static sources, fresh x64/ARM64 host links and selected-directory checks; no game, screen or historical performance suite.',flush=True)
if __name__=='__main__':main()
