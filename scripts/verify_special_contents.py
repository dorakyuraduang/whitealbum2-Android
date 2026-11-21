"""Build/verify the common Special program without starting a game or device UI."""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import sys

from prepare_special_contents import ROOT, digest, write_json

RECEIPT = ROOT/'logs/special-aot-verification.json'


def source_bindings():
    files = set()
    for directory in ('recompiled', 'platform', 'engine', 'hosts', 'android/native',
                      'tools/WindowsRecompTool-main/include', 'tools/WindowsRecompTool-main/src/runtime',
                      'generated/special-contents/aot','third_party/x87_math'):
        files.update(p for p in (ROOT/directory).rglob('*') if p.is_file() and p.suffix in
                     {'.cpp','.cc','.hpp','.h','.inc','.cmake','.txt'})
    files.update(ROOT/name for name in ('config/porting_contract.json', 'config/special_contents.json','config/special_native_contracts.json','config/special_hot_functions.json',
        'scripts/generate_shared_aot.py','scripts/generate_special_aot.py','scripts/prepare_special_contents.py',
        'scripts/verify_special_contents.py','tests/special_aot.cpp','tests/special_runtime.cpp','tests/special_vorbis.cpp','tests/special_vorbis_fixture.hpp','tests/special_render.cpp',
        'tests/special_drawing.cpp','tests/native_sprite_cpu.cpp','tests/glyph_scheduling.cpp','tests/special_sort.cpp','tests/io_diagnostics.cpp',
        'tests/special_save.cpp','tests/save_inventory.cpp','tests/save_thumbnail.cpp',
        'tests/special_weather.cpp','tests/special_language.cpp','tests/special_subtitles.cpp','tests/special_fast_forward.cpp','tests/special_images.cpp','tests/native_resource_cache.cpp','scripts/generate_weather_batch.py',
        'scripts/read_android_counters.py',
        'generated/special-contents/preparation.json','generated/special-contents/aot/manifest.json',
        'generated/special-contents/callback-upgrade/manifest.json','scripts/audit_special_callbacks.py',
        'scripts/analysis/ExportCachedListing.java',
        'third_party/x87_math/UPSTREAM.json','scripts/verify_x87_transcendental.py',
        'logs/x87-transcendental-verification.json',
        'tools/WindowsRecompTool-main/src/pe.cpp','tools/WindowsRecompTool-main/src/resources.cpp'))
    files.update(ROOT/name for name in ('port/native_resources.cpp','port/native_resources.hpp','port/native_vorbis.cpp','port/native_vorbis.hpp','third_party/stb/stb_vorbis.c','port/sprite_sort.cpp','port/sprite_sort.hpp'))
    return {p.relative_to(ROOT).as_posix(): digest(p) for p in sorted(files)}


def compiled_special(directory):
    entries = json.loads((directory/'compile_commands.json').read_text())
    prefix = (ROOT/'generated/special-contents/aot').resolve()
    return {Path(item['file']).resolve().relative_to(ROOT).as_posix(): digest(Path(item['file']))
            for item in entries if Path(item['file']).resolve().is_relative_to(prefix)
            and Path(item['file']).name != 'probe_oracle.cpp'}

def compiled_math(directory):
    entries=json.loads((directory/'compile_commands.json').read_text())
    prefix=(ROOT/'third_party/x87_math').resolve();bridge=(ROOT/'recompiled/transcend.cpp').resolve()
    return {Path(e['file']).resolve().relative_to(ROOT).as_posix():digest(Path(e['file']))
            for e in entries if Path(e['file']).resolve().is_relative_to(prefix) or Path(e['file']).resolve()==bridge}


def compiled_codec(directory):
    wanted={(ROOT/name).resolve() for name in ('port/native_vorbis.cpp','engine/vorbis_stream.cpp')}
    entries=json.loads((directory/'compile_commands.json').read_text())
    return {Path(e['file']).resolve().relative_to(ROOT).as_posix():digest(Path(e['file']))
            for e in entries if Path(e['file']).resolve() in wanted}


def checked_hot_policy(directory):
    import re
    entries=json.loads((directory/'compile_commands.json').read_text())
    generated=(ROOT/'generated/special-contents/aot').resolve()
    policies={}
    for entry in entries:
        path=Path(entry['file']).resolve()
        if not path.is_relative_to(generated) or path.name=='probe_oracle.cpp':continue
        options=re.findall(r'(?<!\S)-O(?:[0123sz]|fast)(?=\s|$)',entry['command'])
        expected='-O2' if path.name=='functions_hot.cpp' else '-Oz'
        if not options or options[-1]!=expected:
            raise ValueError('Special compile optimization differs from measured policy: '+str(path))
        policies[path.name]=expected
    if policies.get('functions_hot.cpp')!='-O2':raise ValueError('Missing measured Special hot source')
    return policies


def compiled_drawing(directory):
    wanted={(ROOT/name).resolve() for name in ('recompiled/native_sprites.cpp','recompiled/special/render.cpp','recompiled/source_sort.cpp')}
    entries=json.loads((directory/'compile_commands.json').read_text())
    return {Path(e['file']).resolve().relative_to(ROOT).as_posix():digest(Path(e['file']))
            for e in entries if Path(e['file']).resolve() in wanted}

def compiled_weather(directory):
    wanted=(ROOT/'recompiled/special/weather.cpp').resolve()
    entries=json.loads((directory/'compile_commands.json').read_text())
    found={Path(e['file']).resolve().relative_to(ROOT).as_posix():digest(Path(e['file'])) for e in entries if Path(e['file']).resolve()==wanted}
    for entry in entries:
        if Path(entry['file']).resolve()==wanted:
            if not all(flag in entry['command'] for flag in ('-O2','-ffp-contract=off','-frounding-math')):
                raise ValueError('Special shared weather arithmetic compile options differ')
    return found


def compiled_subtitles(directory):
    wanted={'engine/special_subtitles.cpp','engine/subtitles.cpp','recompiled/subtitles.cpp','recompiled/special/subtitles.cpp','platform/render/source_graphics.cpp'}
    paths={(ROOT/name).resolve() for name in wanted}
    entries=json.loads((directory/'compile_commands.json').read_text())
    return {Path(e['file']).resolve().relative_to(ROOT).as_posix():digest(Path(e['file'])) for e in entries if Path(e['file']).resolve() in paths}


def compiled_image_resources(directory):
    wanted=(ROOT/'port/native_resources.cpp').resolve()
    entries=json.loads((directory/'compile_commands.json').read_text())
    return {Path(e['file']).resolve().relative_to(ROOT).as_posix():digest(Path(e['file'])) for e in entries if Path(e['file']).resolve()==wanted}


def fresh():
    proof = json.loads(RECEIPT.read_text())
    if not proof['passed'] or proof['input_sha256'] != json.loads((ROOT/'config/special_contents.json').read_text())['sha256']:
        raise ValueError('Missing or wrong Special verification')
    for name, sha in proof['source_sha256'].items():
        if digest(ROOT/name) != sha:
            raise ValueError('Stale Special source verification: '+name)
    for artifact in proof['artifacts'].values():
        if digest(Path(artifact['file'])) != artifact['sha256']:
            raise ValueError('Special artifact changed after verification')
    if 'scalar_math_notice' in proof and digest(Path(proof['scalar_math_notice']['file']))!=proof['scalar_math_notice']['sha256']:
        raise ValueError('Special scalar math license notice changed')
    for report in proof['checks'].values():
        if digest(Path(report['file'])) != report['sha256'] or not json.loads(Path(report['file']).read_text())['passed']:
            raise ValueError('Special memory check changed')
        if digest(Path(report['binary'])) != report['binary_sha256']:
            raise ValueError('Special memory check binary changed')
    return proof


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--record-only', action='store_true', help='Verify already-completed serial builds and memory runs')
    parser.add_argument('--check', action='store_true', help='Only validate an existing source-bound receipt')
    args = parser.parse_args()
    if args.check:
        proof=fresh();print('Special source-bound verification is current. Device gameplay remains pending.');return
    flags = getattr(subprocess,'CREATE_NO_WINDOW',0) | getattr(subprocess,'BELOW_NORMAL_PRIORITY_CLASS',0)
    env=os.environ.copy();env.update(CMAKE_BUILD_PARALLEL_LEVEL='1',
        ZIG_GLOBAL_CACHE_DIR=str(ROOT/'build/zig-cache'),ZIG_LOCAL_CACHE_DIR=str(ROOT/'build/zig-local'),
        TEMP=str(ROOT/'build/temp'),TMP=str(ROOT/'build/temp'))
    win=ROOT/'build/shared-host-win64';android=ROOT/'build/shared-aot-android'
    cmake=ROOT/'tools/python/cmake/data/bin/cmake.exe';ndk=ROOT/'tools/android-sdk/ndk/27.0.12077973'
    acmake=ROOT/'tools/android-sdk/cmake/3.22.1/bin/cmake.exe'
    with (ROOT/'logs/special-verification.log').open('wb') as log:
        def run(command,timeout=180):
            subprocess.run(list(map(str,command)),cwd=ROOT,env=env,creationflags=flags,
                stdout=log,stderr=subprocess.STDOUT,check=True,timeout=timeout)
        if not args.record_only:
            from verify_x87_transcendental import fresh as math_fresh
            try:math_fresh()
            except (OSError,ValueError):run([sys.executable,ROOT/'scripts/verify_x87_transcendental.py'],600)
            run([sys.executable,ROOT/'scripts/audit_special_callbacks.py'],600)
            run([sys.executable,ROOT/'scripts/generate_special_aot.py'])
            # Shared thumbnail code is also an input of the cached Main text
            # derivative used by regression checks. Never reopen its EXE.
            run([sys.executable,ROOT/'scripts/prepare_shared_text_flow.py'])
            run([cmake,'-S',ROOT/'hosts','-B',win,'-G','Ninja',
                '-DCMAKE_TOOLCHAIN_FILE='+str(ROOT/'scripts/zig-toolchain.cmake'),
                '-DCMAKE_BUILD_TYPE=MinSizeRel','-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',
                '-DWA2_SPECIAL_CONTENTS=ON','-DWA2_SPECIAL_CHECKS=ON',
                '-DWA2_SHARED_SDL_ARCHIVE='+str(ROOT/'build/test-content-046/materials/windows/archives/libSDL2.a')])
            run([cmake,'--build',win,'--target','wa2_special_memory_tests','wa2_special_runtime_tests','wa2_special_codec_tests','wa2_special_render_tests',
                 'wa2_special_drawing_tests','wa2_special_main_sprite_tests','wa2_special_main_glyph_tests','wa2_special_sort_tests','wa2_special_io_tests',
                 'wa2_special_save_tests','wa2_special_main_save_tests','wa2_special_main_thumbnail_tests','wa2_special_weather_tests','wa2_special_language_tests','wa2_special_subtitle_tests','wa2_special_fast_tests','wa2_special_image_tests','wa2_special_main_resource_tests','wa2_special_windows','--parallel','1'],7200)
            for stem in ('memory','runtime','codec','render','drawing','main_sprite','main_glyph','sort','io','save','main_save','main_thumbnail','weather','language','subtitle','fast','image','main_resource'):
                command=[win/f'shared_runtime/special/wa2_special_{stem}_tests.exe',ROOT/f'logs/special-{stem}-check.json']
                if stem=='main_sprite':command.append('--correctness-only')
                run(command,60)
            run([acmake,'-S',ROOT/'tests/shared-aot-android','-B',android,'-G','Ninja',
                '-DCMAKE_MAKE_PROGRAM='+str(ROOT/'tools/python/bin/ninja.exe'),
                '-DCMAKE_TOOLCHAIN_FILE='+str(ndk/'build/cmake/android.toolchain.cmake'),
                '-DANDROID_ABI=arm64-v8a','-DANDROID_PLATFORM=android-26','-DANDROID_STL=c++_shared',
                '-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON','-DCMAKE_BUILD_TYPE=MinSizeRel',
                '-DWA2_AOT_DIAGNOSTICS=OFF','-DWA2_SPECIAL_CONTENTS=ON','-DWA2_SPECIAL_CHECKS=OFF',
                '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON'])
            run([acmake,'--build',android,'--target','wa2_special','--parallel','1'],7200)
        manifest_path=ROOT/'generated/special-contents/aot/manifest.json'
        manifest=json.loads(manifest_path.read_text())
        from audit_special_callbacks import validated_cache
        callbacks=validated_cache()
        from verify_x87_transcendental import fresh as math_fresh
        math_proof=math_fresh()
        for name,sha in manifest['generated_sha256'].items():
            if digest(manifest_path.parent/name)!=sha:raise ValueError('Special generated file changed: '+name)
        windows_sources,android_sources=compiled_special(win),compiled_special(android)
        expected={f'generated/special-contents/aot/{name}':sha for name,sha in manifest['generated_sha256'].items()
                  if name.endswith('.cpp') and name!='probe_oracle.cpp'}
        if windows_sources!=android_sources or windows_sources!=expected:
            raise ValueError('Windows/Android compiled different Special C++ source inventories')
        hot_policy=checked_hot_policy(win)
        if hot_policy!=checked_hot_policy(android):raise ValueError('Host hot optimization policies differ')
        if (manifest.get('measured_hot_entries')!=[0x43cb90] or
                not manifest.get('shared_register_fp_and_integer_kernels')):
            raise ValueError('Missing measured Special shared register kernels')
        math_windows,math_android=compiled_math(win),compiled_math(android)
        math_expected={p.relative_to(ROOT).as_posix():digest(p) for p in (ROOT/'third_party/x87_math').rglob('*.cc')}
        math_expected['recompiled/transcend.cpp']=digest(ROOT/'recompiled/transcend.cpp')
        if math_windows!=math_android or math_windows!=math_expected:
            raise ValueError('Windows/Android compiled different scalar x87 math sources')
        codec_windows,codec_android=compiled_codec(win),compiled_codec(android)
        codec_expected={name:digest(ROOT/name) for name in ('port/native_vorbis.cpp','engine/vorbis_stream.cpp')}
        if codec_windows!=codec_android or codec_windows!=codec_expected:
            raise ValueError('Windows/Android compiled different shared Vorbis sources')
        drawing_sources=compiled_drawing(win)
        if (drawing_sources!=compiled_drawing(android) or
                set(drawing_sources)!={'recompiled/native_sprites.cpp','recompiled/special/render.cpp','recompiled/source_sort.cpp'} or
                manifest.get('shared_sprite_and_glyph_entries')!=[0x43cb90,0x447d70]):
            raise ValueError('Missing identical shared main/Special drawing implementation')
        if compiled_weather(win)!=compiled_weather(android) or set(compiled_weather(win))!={'recompiled/special/weather.cpp'}:
            raise ValueError('Missing identical Special shared weather source on both hosts')
        image_sources=compiled_image_resources(win)
        if image_sources!=compiled_image_resources(android) or len(image_sources)!=1:
            raise ValueError('Both hosts must compile the same shared image resource adapter')
        subtitle_sources=compiled_subtitles(win)
        if subtitle_sources!=compiled_subtitles(android) or len(subtitle_sources)!=5:
            raise ValueError('Windows/Android must compile the identical SC/shared subtitle core')
        if manifest['pending_transcendental_sites'] or len(manifest.get('supported_transcendental_sites',[]))!=50:
            raise ValueError('Special transcendental sites are not all statically supported')
        if manifest.get('shared_vorbis_host_boundaries')!=[0x458630,0x458660,0x458700,0x458890]:
            raise ValueError('Missing Special shared codec host boundaries')
        if manifest.get('shared_subtitle_events')!=json.loads((ROOT/'config/special_native_contracts.json').read_text())['subtitles']:
            raise ValueError('Missing SHA-bound SC BGM/chapter subtitle events')
        checks={}
        for stem in ('memory','runtime','codec','render','drawing','main_sprite','main_glyph','sort','io','save','main_save','main_thumbnail','weather','language','subtitle','fast','image','main_resource'):
            path=ROOT/f'logs/special-{stem}-check.json';check=json.loads(path.read_text())
            if not check['passed'] or check['game_launched'] or check['screen_access']:
                raise ValueError('Invalid Special headless verification')
            if stem=='fast' and (check.get('source_predicate_comparisons')!=4608 or not all(check.get(k) for k in ('original_sleep_caller_checked','settings_save_load_pacing_preserved','all_draws_preserved'))):
                raise ValueError('Missing SC source/fast-forward PC component checks')
            if stem=='image' and (check.get('source_image_comparisons')!=30 or not check.get('dictionary_and_trace_preserved') or not check.get('original_static_loader_used')):
                raise ValueError('Missing SC source/image PC component checks')
            if stem=='subtitle' and (check.get('static_return_comparisons')!=8 or check.get('main_cue_regressions')!=315 or not all(check.get(k) for k in ('sc_dialogue_secondary_and_song_merged','chapter_delay_checked','disabled_skips_guest_reads','source_ret_fault_precedes_observer'))):
                raise ValueError('Missing shared SC caption/state/source-RET checks')
            if stem=='language' and (check.get('japanese_alias_cases')!=10 or check.get('guards')!=7 or not all(check.get(k) for k in ('chinese_banks_preserved','main_pairs_preserved','loose_resources_routed','patch_priority_preserved','missing_pack_atomic_failure'))):
                raise ValueError('Missing common Main/SC language resource routing checks')
            if stem=='memory' and (check.get('string_regression_cases')!=6 or
                    check.get('reported_rep_address')!=4691986 or not check.get('string_fault_prefix_checked') or
                    not check.get('diagnostic_budget_preserved')):
                raise ValueError('Missing Special REP metering/fault-prefix regression checks')
            if stem=='memory' and (check.get('reported_callback_address')!=4739667 or
                    check.get('recovered_callback_entries_checked')!=len(callbacks['recovered_entries']) or
                    check.get('instruction_starts_checked')!=manifest['instruction_locations']):
                raise ValueError('Missing Special recovered callback catalogue checks')
            if stem=='runtime' and (check.get('reported_callback_address')!=4739667 or
                    not check.get('crt_initializer_registered_filter') or check.get('crt_filter_rejection_cases')!=3 or
                    check.get('game_startup_called') is not False or check.get('bounded_static_callback_entries',0)<4):
                raise ValueError('Missing bounded Special CRT callback regression checks')
            if stem=='memory' and (check.get('reported_copy_address')!=4673248 or check.get('crt_copy_cases',0)<3000 or
                    check.get('recovered_jump_entries_checked')!=len(callbacks['recovered_jump_entries']) or
                    not check.get('shared_bulk_string_exercised')):
                raise ValueError('Missing Special CRT switch/copy/bulk-string regression checks')
            if stem=='runtime' and not check.get('special_final_cpu_and_storage_counters'):
                raise ValueError('Missing Special final CPU/storage diagnostics')
            if stem=='memory' and (check.get('reported_transcendental_address')!=4642967 or check.get('generated_fyl2x_prefix_cases')!=24):
                raise ValueError('Missing actual Special FYL2X prefix regression')
            if stem=='runtime' and (check.get('lzss_original_tail_cases')!=100 or not check.get('lzss_output_dictionary_stack_fp_agree')):
                raise ValueError('Missing Special LZSS original-loop/tail regression')
            if stem=='runtime' and check.get('lzss_admission_declines_checked')!=8:
                raise ValueError('Missing Special LZSS decline-without-mutation checks')
            if stem=='runtime' and (check.get('unmapped_storage_probe_cases')!=34 or
                    not check.get('unmapped_storage_provider_untouched') or not check.get('canonical_save_sharing_preserved')):
                raise ValueError('Missing Special missing-file/access-denied storage probe regression')
            if stem=='memory' and check.get('codec_host_boundary_cases')!=6:
                raise ValueError('Missing actual Special direct/indirect codec boundaries')
            if stem=='codec' and (check.get('source_reference_cases')!=2 or check.get('shared_codec_cases')!=28 or
                    check.get('guard_cases')!=6 or check.get('maximum_sample_error',999)>16 or
                    check.get('maximum_rms_sample_error',999)>1.5 or check.get('original_audio_used') is not False or
                    check.get('audio_device_opened') is not False):
                raise ValueError('Missing Special codec ABI/source PCM regression')
            if stem=='render' and (check.get('hot_entry')!=0x43cb90 or
                    check.get('source_differential_cases')!=264 or check.get('precise_fault_cases')!=24 or
                    not check.get('exact_cpu_fp_stack_data_globals_and_draw_calls') or
                    not check.get('paged_and_linear_memory') or not check.get('outer_and_leaf_api_boundaries') or
                    not check.get('serialized_source_reference') or check.get('window_or_device_created') is not False):
                raise ValueError('Missing Special actual drawing source/state regression')
            if stem=='drawing' and (check.get('sprite_source_cases')!=288 or check.get('glyph_source_cases')!=224 or
                    check.get('decline_cases')!=12 or check.get('immutable_code_guards')!=4 or
                    check.get('glyph_budget_yield_cases')!=4 or
                    check.get('glyph_command_replay_hits',0)<1 or
                    not check.get('raw_vertices_render_state_and_game_objects_equal') or
                    not check.get('cdecl_nonvolatile_fp_control_and_host_environment_preserved') or
                    not check.get('same_main_drawing_implementation') or check.get('window_or_device_created') is not False):
                raise ValueError('Missing Special shared sprite/glyph source comparison')
            if stem=='main_sprite' and (not check.get('vertex_bits_and_render_state_equal') or
                    not check.get('void_cdecl_abi_preserved') or check.get('performance_samples_rerun') is not False):
                raise ValueError('Missing main default drawing-profile correctness regression')
            if stem=='main_glyph' and (check.get('glyphs')!=768 or check.get('deferrals')!=2 or
                    not check.get('source_entry_yields_checked') or not check.get('unchanged_cpu_stack_on_defer')):
                raise ValueError('Missing main default glyph scheduling regression')
            if stem=='sort' and (check.get('main_source_cases')!=160 or check.get('special_source_cases')!=160 or
                    check.get('decline_cases')!=20 or not check.get('exact_cpu_fp_flags_arrays_pointer_and_fault_prefix') or
                    not check.get('duplicate_signed_and_sentinel_order') or not check.get('same_main_special_implementation') or
                    check.get('window_or_device_created') is not False):
                raise ValueError('Missing complete Main/Special source sorting comparisons')
            if stem=='io' and (not check.get('shared_render_sort_progress_counters') or not check.get('rate_and_count_bounded') or
                    not check.get('guest_errors_preserved') or check.get('selected_directory_written') is not False):
                raise ValueError('Missing bounded live render/sort diagnostics regression')
            if stem=='save' and (check.get('actual_static_save_reader_calls',0)<240 or check.get('actual_preview_stack_checks')!=2 or
                    check.get('exact_thumbnail_cases')!=24 or check.get('thumbnail_hits',0)<6 or check.get('actual_movie_profile_and_filename_cases')!=58 or not check.get('payload_reads_fresh')):
                raise ValueError('Missing actual SC save reader/frame/thumbnail comparisons')
            if stem=='main_save' and (check.get('actual_static_reader_calls',0)<800 or not check.get('page_preview_mode_transition_checked')):
                raise ValueError('Missing main save policy regression')
            if stem=='main_thumbnail' and (check.get('cases')!=24 or not check.get('exact_cpu_ram_and_faults')):
                raise ValueError('Missing main thumbnail prefix regression')
            if stem=='weather' and (check.get('main_source_cases')!=42 or check.get('special_source_cases')!=42 or
                    check.get('accepted_batches',0)<84 or check.get('exact_prefix_yields')!=40 or
                    not check.get('source_oracle_component_only') or not check.get('full_cpu_fp_ram_equal') or check.get('unchanged_declines')!=12):
                raise ValueError('Missing shared Main/SC weather source comparisons')
            binary=win/f'shared_runtime/special/wa2_special_{stem}_tests.exe'
            checks[stem]=dict(file=str(path),sha256=digest(path),binary=str(binary),binary_sha256=digest(binary))
        if manifest.get('shared_sort_prefix',{}).get('begin')!=0x443c90 or manifest['shared_sort_prefix']['end']!=0x443d7c:
            raise ValueError('Missing verified Special sorting prefix')
        output=ROOT/'out/special-contents';output.mkdir(parents=True,exist_ok=True)
        strip=ndk/'toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-strip.exe'
        artifacts={}
        for name,source in {'windows':win/'WA2_Special_SDL2_x64.exe','android':android/'actual_host/libwa2_special.so'}.items():
            target=output/source.name
            run([strip,'--strip-all','-o',target,source])
            artifacts[name]=dict(file=str(target),sha256=digest(target),bytes=target.stat().st_size,
                                 unstripped_file=str(source),unstripped_sha256=digest(source))
        data=Path(artifacts['android']['file']).read_bytes()
        notice=output/'X87-MATH-LICENSES.txt';notice.write_bytes((ROOT/'third_party/x87_math/NOTICE.txt').read_bytes())
        if data[:5]!=b'\x7fELF\x02' or struct.unpack_from('<H',data,18)[0]!=183:
            raise ValueError('Special library is not ELF64 ARM64')
        offset=struct.unpack_from('<Q',data,32)[0];size,count=struct.unpack_from('<HH',data,54)
        load_alignments=[struct.unpack_from('<Q',data,offset+n*size+48)[0] for n in range(count)
                         if struct.unpack_from('<I',data,offset+n*size)[0]==1]
        if not load_alignments or any(a<16384 for a in load_alignments):raise ValueError('Special ELF lacks 16 KiB load alignment')
        nm=ndk/'toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-nm.exe'
        symbol_audits={}
        for name,artifact in artifacts.items():
            # Zig's MinSizeRel PE has no public symbol table. Audit the actual
            # compiled archive on Windows and the final linked ELF on Android;
            # do not represent an unavailable PE symbol audit as completed.
            symbol_input=win/'shared_runtime/special/libwa2_special_aot.a' if name=='windows' else Path(artifact['unstripped_file'])
            symbol_inputs=[symbol_input]
            if name=='windows':symbol_inputs.extend([win/'shared_runtime/libwa2_x87_transcend.a',
                win/'shared_runtime/libwa2_function_process.a',win/'shared_runtime/native_engine/libwa2_engine.a',
                win/'shared_runtime/libwa2_source_drawing.a',win/'shared_runtime/special/libwa2_special_process.a'])
            symbols=subprocess.run([str(nm),'--defined-only','--demangle',*map(str,symbol_inputs)],
                cwd=ROOT,env=env,creationflags=flags,stdout=subprocess.PIPE,stderr=subprocess.PIPE,
                check=True,timeout=60).stdout.decode('utf-8',errors='replace')
            audit=dict(method='compiled-archive-symbols' if name=='windows' else 'actual-linked-symbols',
                final_binary_symbols_available=name!='windows',special_static_functions='wa2::special::f_' in symbols,
                shared_bulk_string_kernel='wa2::aot::try_bulk_string(' in symbols,
                scalar_x87_math='fyl2x(' in symbols and 'fsin(' in symbols and 'fcos(' in symbols,
                shared_vorbis_codec='wa2::audio::VorbisStream::push(' in symbols and
                    'wr::install_wa2_native_vorbis(wr::Process&, wr::NativeVorbisProfile' in symbols,
                shared_source_drawing='wa2::aot::native_draw_sprite(wr::Cpu&, wr::Memory&, wa2::aot::SpriteSource const&)' in symbols and
                    'wa2::aot::native_draw_glyph_or_yield(wr::Cpu&, wr::Memory&, wa2::aot::SpriteSource const&)' in symbols,
                shared_source_sort='wa2::aot::source_sort_full(wr::Cpu&, wr::Memory&, wa2::aot::SortSource const&)' in symbols,
                shared_image_resource_adapter='wr::install_wa2_native_resources(wr::Process&, wr::NativeResourceProfile, bool)' in symbols,
                shared_source_subtitles='wa2::special::SubtitleState::returned(' in symbols and 'wa2::subtitles::compile_special_binary(' in symbols,
                shared_source_weather='wa2::special::update_weather(wr::Cpu&, wr::Memory&)' in symbols and 'weather_408507(' in symbols,
                main_story_static_functions='wa2::aot::f_' in symbols,
                runtime_instruction_decoder='wr::Decoder::' in symbols)
            if not audit['special_static_functions'] or not audit['shared_bulk_string_kernel'] or not audit['scalar_x87_math'] or not audit['shared_vorbis_codec'] or not audit['shared_source_drawing'] or not audit['shared_source_sort'] or not audit['shared_source_weather'] or not audit['shared_source_subtitles'] or not audit['shared_image_resource_adapter'] or audit['main_story_static_functions'] or audit['runtime_instruction_decoder']:
                raise ValueError('Special link contains the wrong game catalogue or an instruction decoder: '+name)
            symbol_audits[name]=audit
        proof=dict(schema='wa2.special-aot-verification.v1',input_sha256=manifest['input_sha256'],
            same_special_sources_on_all_hosts=True,compiled_shared_sources=windows_sources,
            compiled_scalar_math_sources=math_windows,transcendental_hardware_cases=math_proof['cases'],
            compiled_shared_codec_sources=codec_windows,shared_vorbis_host_boundaries=manifest['shared_vorbis_host_boundaries'],
            compiled_shared_image_resources=image_sources,compiled_shared_subtitle_sources=subtitle_sources,compiled_shared_drawing_sources=drawing_sources,shared_sprite_and_glyph_entries=manifest['shared_sprite_and_glyph_entries'],
            shared_sort_prefix=manifest['shared_sort_prefix'],
            measured_hot_entries=manifest['measured_hot_entries'],compiled_optimization_policy=hot_policy,
            scalar_math_notice=dict(file=str(notice),sha256=digest(notice)),
            instruction_locations=manifest['instruction_locations'],static_bodies=manifest['static_bodies'],
            recovered_callback_entries=len(callbacks['recovered_entries']),pointer_slots_audited=callbacks['pointer_slots_audited'],
            recovered_jump_entries=len(callbacks['recovered_jump_entries']),shared_bulk_string_kernel=True,
            pending_transcendental_sites=manifest['pending_transcendental_sites'],
            supported_transcendental_sites=manifest['supported_transcendental_sites'],native_lzss_prefix=True,
            source_sha256=source_bindings(),checks=checks,artifacts=artifacts,elf_load_alignments=load_alignments,symbol_audits=symbol_audits,
            original_main_executable_opened=False,source_executable_executed=False,
            game_launched=False,screen_access=False,android_gameplay_validation='pending-user-controlled-run',
            android_performance_validation='pending-comparable-device-run',complete_gameplay_verified=False,passed=True)
        write_json(RECEIPT,proof)
    print(json.dumps(dict(passed=True,shared_sources=len(windows_sources),artifacts=artifacts,android_gameplay_validation='pending-user-controlled-run')),flush=True)


if __name__=='__main__':
    main()
