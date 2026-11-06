"""Build the shared native migration core and run synthetic state checks.

No game, emulator, screen access, original EXE or user save is used.
Optional original-resource checks parse structure and image headers only.
The linked core is incomplete and is not packaged as a playable application.
"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]


def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--zig-cache',type=Path,default=ROOT/'build/zig-cache')
    parser.add_argument('--resource-root',type=Path,help='Optionally parse external ck-gal.pak script records; never execute them')
    parser.add_argument('--sdl-archive',type=Path,help='Optional existing Windows SDL2 archive for finite owned-memory composition checks')
    parser.add_argument('--shader-compiler',type=Path,default=ROOT/'tools/glslang/16.6.0/bin/glslang.exe',help='Offline GLSL compiler; no GL context is created')
    args=parser.parse_args()
    if os.name!='nt':raise SystemExit('This local verifier requires the existing Windows Zig/Android NDK tools')
    kernel=ctypes.WinDLL('kernel32',use_last_error=True);kernel.GetCurrentProcess.restype=ctypes.c_void_p
    kernel.SetPriorityClass.argtypes=[ctypes.c_void_p,ctypes.c_uint]
    kernel.GetProcessAffinityMask.argtypes=[ctypes.c_void_p,ctypes.POINTER(ctypes.c_size_t),ctypes.POINTER(ctypes.c_size_t)]
    kernel.SetProcessAffinityMask.argtypes=[ctypes.c_void_p,ctypes.c_size_t]
    process=kernel.GetCurrentProcess()
    if not kernel.SetPriorityClass(process,0x4000):raise ctypes.WinError()
    current=ctypes.c_size_t();system=ctypes.c_size_t()
    if not kernel.GetProcessAffinityMask(process,ctypes.byref(current),ctypes.byref(system)):raise ctypes.WinError()
    if not kernel.SetProcessAffinityMask(process,current.value&-current.value):raise ctypes.WinError()
    flags=subprocess.CREATE_NO_WINDOW|subprocess.BELOW_NORMAL_PRIORITY_CLASS
    build=ROOT/'build/native-core';build.mkdir(parents=True,exist_ok=True)
    (ROOT/'logs').mkdir(exist_ok=True)
    env=os.environ.copy();env['CMAKE_BUILD_PARALLEL_LEVEL']='1'
    for name,path in [('TEMP',build/'temp'),('TMP',build/'temp'),('ZIG_GLOBAL_CACHE_DIR',args.zig_cache.resolve()),('ZIG_LOCAL_CACHE_DIR',build/'zig-local')]:
        path.mkdir(parents=True,exist_ok=True);env[name]=str(path)
    inputs=[ROOT/'config/porting_contract.json',Path(__file__),ROOT/'native/CMakeLists.txt',ROOT/'game/CMakeLists.txt',
        ROOT/'tests/native_progress.cpp',ROOT/'tests/native_script_state.cpp',ROOT/'tests/native_control_state.cpp',ROOT/'tests/native_camera_state.cpp',ROOT/'tests/native_script_catalogue.cpp',ROOT/'tests/native_core_link.cpp',ROOT/'platform/progress_storage.cpp',ROOT/'platform/progress_storage.hpp',ROOT/'platform/file_io.cpp',ROOT/'platform/file_io.hpp',ROOT/'platform/sdl_scene.cpp',ROOT/'platform/sdl_scene.hpp',ROOT/'platform/sdl_canvas.hpp',
        ROOT/'scripts/zig-toolchain.cmake',ROOT/'tests/native_voices.cpp',ROOT/'tests/native_novel_state.cpp',ROOT/'tests/native_weather_state.cpp',ROOT/'tests/native_calendar_state.cpp',ROOT/'tests/native_animation_state.cpp',ROOT/'tests/native_scene_geometry.cpp',ROOT/'tests/native_shake_state.cpp',ROOT/'tests/native_world_state.cpp',ROOT/'tests/native_scene_composition.cpp',ROOT/'tests/native_image_styles.cpp',ROOT/'tests/native_caption_state.cpp',ROOT/'tests/native_selection_state.cpp',ROOT/'tests/native_mask_sources.cpp',ROOT/'platform/sdl_canvas.cpp',ROOT/'platform/sdl_canvas.cmake',ROOT/'platform/sdl_mask.cpp',ROOT/'platform/sdl_mask.hpp',ROOT/'platform/mask_shaders.cpp',ROOT/'platform/mask_shaders.hpp']
    inputs += [ROOT/'tests/native_system_sounds.cpp',ROOT/'tests/native_lifecycle_state.cpp',ROOT/'tests/native_stream_voice.cpp',ROOT/'tests/native_dialogue_state.cpp',ROOT/'tests/native_sprite_atlas.cpp',ROOT/'tests/native_font_coverage.cpp',ROOT/'tests/native_frame_batch.cpp',ROOT/'tests/native_backlog_state.cpp']
    inputs += [p for folder in ('engine','game') for p in (ROOT/folder).glob('*') if p.suffix in ('.cpp','.hpp','.inc','.cmake','.txt')]
    inputs += [p for p in (ROOT/'third_party/wa2_subtitles').glob('*.inc')]
    bindings={p.relative_to(ROOT).as_posix():digest(p) for p in inputs}
    cmake=ROOT/'tools/python/cmake/data/bin/cmake.exe';ninja=ROOT/'tools/python/bin/ninja.exe'
    ndk=ROOT/'tools/android-sdk/ndk/27.0.12077973';llvm=ndk/'toolchains/llvm/prebuilt/windows-x86_64/bin'
    def run(command,console,timeout=600):
        command=list(map(str,command));console.write(('RUN '+subprocess.list2cmdline(command)+'\n').encode());console.flush()
        with subprocess.Popen(command,cwd=ROOT,env=env,creationflags=flags,stdout=console,stderr=subprocess.STDOUT) as child:
            try:status=child.wait(timeout)
            except subprocess.TimeoutExpired:
                subprocess.run(['taskkill','/PID',str(child.pid),'/T','/F'],creationflags=flags,stdout=console,stderr=subprocess.STDOUT,timeout=20)
                child.wait();raise
            if status:raise subprocess.CalledProcessError(status,command)
    def output(command):
        return subprocess.run(list(map(str,command)),cwd=ROOT,env=env,creationflags=flags,capture_output=True,text=True,encoding='utf-8',errors='replace',check=True,timeout=60).stdout
    sources={};artifacts={};codec_bindings={};composition_state=None;composition_artifact=None
    composition_sdl_sha=digest(args.sdl_archive) if args.sdl_archive else None
    shader_compiler_sha=digest(args.shader_compiler);mask_shader_checks=[]
    with (ROOT/'logs/native-core-build.log').open('wb') as console:
        for host in ('windows','android'):
            directory=build/host
            configure=[cmake,'-S',ROOT/'native','-B',directory,'-G','Ninja','-DCMAKE_BUILD_TYPE=MinSizeRel',
                '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON','-DCMAKE_MAKE_PROGRAM='+str(ninja)]
            if host=='windows':configure+=['-DCMAKE_TOOLCHAIN_FILE='+str(ROOT/'scripts/zig-toolchain.cmake'),'-DWA2_NATIVE_SDL_ARCHIVE='+(str(args.sdl_archive.resolve()) if args.sdl_archive else '')]
            else:configure+=['-DCMAKE_TOOLCHAIN_FILE='+str(ndk/'build/cmake/android.toolchain.cmake'),'-DANDROID_ABI=arm64-v8a','-DANDROID_PLATFORM=android-26','-DANDROID_STL=c++_shared']
            print('Configuring and building '+host+' native core; one hidden low-priority job.',flush=True)
            run(configure,console)
            targets=['wa2_native_link_check','wa2_native_presentation_check']+(['wa2_native_progress_tests','wa2_native_script_state_tests','wa2_native_control_state_tests','wa2_native_camera_state_tests','wa2_native_voice_state_tests','wa2_native_novel_state_tests','wa2_native_weather_state_tests','wa2_native_calendar_state_tests','wa2_native_animation_state_tests','wa2_native_scene_geometry_tests','wa2_native_shake_state_tests','wa2_native_world_state_tests','wa2_native_image_style_tests','wa2_native_caption_state_tests','wa2_native_selection_state_tests'] if host=='windows' else [])
            if host=='windows':targets+=['wa2_native_system_sound_tests','wa2_native_lifecycle_state_tests','wa2_native_stream_voice_tests','wa2_native_dialogue_state_tests','wa2_native_sprite_atlas_tests','wa2_native_font_coverage_tests','wa2_native_frame_batch_tests','wa2_native_backlog_state_tests']
            if host=='windows' and args.resource_root:targets.append('wa2_native_script_catalogue_tests')
            if host=='windows' and args.sdl_archive:targets.append('wa2_native_scene_composition_tests')
            if host=='windows':targets.append('wa2_native_mask_source_tests')
            run([cmake,'--build',directory,'--target',*targets,'--parallel','1'],console,2400)
            commands=json.loads((directory/'compile_commands.json').read_text())
            names={Path(item['file']).resolve().relative_to(ROOT).as_posix() for item in commands}
            allowed=('engine/','game/','platform/progress_storage.cpp','platform/file_io.cpp','platform/sdl_scene.cpp','platform/sdl_canvas.cpp','platform/sdl_mask.cpp','platform/mask_shaders.cpp','tests/native_core_link.cpp','tests/native_progress.cpp','tests/native_script_state.cpp','tests/native_control_state.cpp','tests/native_camera_state.cpp','tests/native_script_catalogue.cpp','tests/native_voices.cpp','tests/native_novel_state.cpp','tests/native_weather_state.cpp','tests/native_calendar_state.cpp','tests/native_animation_state.cpp','tests/native_scene_geometry.cpp','tests/native_shake_state.cpp','tests/native_world_state.cpp','tests/native_scene_composition.cpp','tests/native_image_styles.cpp','tests/native_caption_state.cpp','tests/native_selection_state.cpp','tests/native_mask_sources.cpp','tests/pcm_audio.cpp')
            allowed+=('tests/native_system_sounds.cpp','tests/native_lifecycle_state.cpp','tests/native_stream_voice.cpp','tests/native_dialogue_state.cpp','tests/native_sprite_atlas.cpp','tests/native_font_coverage.cpp','tests/native_frame_batch.cpp','tests/native_backlog_state.cpp')
            if any(not name.startswith(allowed) for name in names):raise ValueError('Non-native source in core compile graph: '+str(sorted(names)))
            # Inspect the actual Ninja dependency closure, not only filenames in
            # a CMake declaration. No old AOT archive can hide in the link inputs.
            target='libwa2_native_link_check.dll' if host=='windows' else 'libwa2_native_link_check.so'
            graph=output([ninja,'-C',directory,'-t','graph',target]).replace('\\','/').lower()
            forbidden=('recompiled/','generated/','winrecomp','wa2_function_process','wa2_shared_aot','/port/')
            if any(token in graph for token in forbidden):raise ValueError('Recompilation dependency in native link graph')
            # compile_commands also lists unused CMake targets. Count only
            # object files present in this library's actual Ninja closure.
            linked_sources=[]
            for item in commands:
                obj=Path(item['output']).resolve()
                relative=obj.relative_to(directory.resolve()).as_posix().lower()
                if relative not in graph:continue
                if not obj.is_file():raise ValueError('Missing compiled native object: '+str(obj))
                name=Path(item['file']).resolve().relative_to(ROOT).as_posix()
                if name.startswith(('engine/','game/','platform/progress_storage.cpp')):linked_sources.append(name)
            sources[host]=sorted(set(linked_sources))
            if not {'game/scene.cpp','engine/scene_progress.cpp','engine/progress.cpp','engine/script_runtime.cpp','platform/progress_storage.cpp'}.issubset(sources[host]):
                raise ValueError('Native source/link inventory is incomplete')
            library=directory/target
            archives=sorted(path for path in directory.rglob('*.a') if not path.name.endswith('.dll.a'))
            if host=='windows':
                # MinSizeRel PE images omit the COFF symbol table. Check the
                # real exported factories and the native archives in its
                # already-audited dependency closure instead of assuming nm
                # can recover discarded local symbols from the DLL.
                exports=output([llvm/'llvm-readobj.exe','--coff-exports',library])
                if 'Arch: x86_64' not in exports:raise ValueError('Native Windows component has wrong architecture')
                symbols=exports+output([llvm/'llvm-nm.exe','--defined-only','--demangle',*archives])
            else:symbols=output([llvm/'llvm-nm.exe','--defined-only','--demangle',library])
            for forbidden_symbol in ('wr::','wa2::aot::','code_page_','native_backend_'):
                if forbidden_symbol in symbols:raise ValueError('Recompilation symbol in native library: '+forbidden_symbol)
            for required in ('wa2_native_link_create','wa2_native_link_tick','wa2_native_link_start','wa2::game::Scene::','wa2::script::SceneProgress::','wa2::progress::State::'):
                if required not in symbols:raise ValueError('Native dependency not actually linked: '+required)
            if host=='android':
                header=output([llvm/'llvm-readelf.exe','-h',library])
                if 'AArch64' not in header:raise ValueError('Native Android component has wrong architecture')
            artifacts[host]=dict(file=str(library),sha256=digest(library),bytes=library.stat().st_size,
                native_archives_sha256={p.relative_to(directory).as_posix():digest(p) for p in archives},
                symbol_audit_scope='PE exports and link-input archives' if host=='windows' else 'linked ELF symbols',
                compile_commands_sha256=digest(directory/'compile_commands.json'),native_link_graph_sha256=hashlib.sha256(graph.encode()).hexdigest())
            codec_host='windows' if host=='windows' else 'arm64'
            for path in (ROOT/'tools/media-codec'/codec_host/'lib').glob('*.a'):codec_bindings[path.relative_to(ROOT).as_posix()]=digest(path)
            if host=='windows':
                print('Running synthetic native script/progress checks; no game starts.',flush=True)
                run([directory/'wa2_native_progress_tests.exe',directory/'native-progress.json'],console,90)
                checks=json.loads((directory/'native-progress.json').read_text())
                if (not checks['passed'] or checks['flag_cases']!=30 or checks['gallery_cases']!=32 or checks['history_cases']!=20 or
                    checks['script_calls']!=5 or checks['io_cases']!=7 or checks['guard_cases']!=25 or checks['flag_write_bytes']!=4096 or
                    checks['serialized_bytes']!=2528384 or not checks['failed_writes_retryable'] or checks['original_resources_opened'] or checks['game_launched']):
                    raise ValueError('Incomplete native progress checks: '+str(checks))
                run([directory/'wa2_native_script_state_tests.exe',directory/'native-script-state.json'],console,90)
                script_state=json.loads((directory/'native-script-state.json').read_text())
                if (not script_state['passed'] or script_state['cases']<7 or script_state['guards']<14 or script_state['resumed_callbacks']!=3 or
                    not all(script_state[key] for key in ('nested_call_restore','pending_command_restore','exact_float_bits','choice_original_ordinals','failed_restore_transactional','synthetic_only')) or
                    script_state['game_launched'] or script_state['screen_access']):
                    raise ValueError('Incomplete native script-state checks: '+str(script_state))
                catalogue=None
                run([directory/'wa2_native_control_state_tests.exe',directory/'native-control-state.json'],console,90)
                control_state=json.loads((directory/'native-control-state.json').read_text())
                if not control_state['passed'] or control_state['cases']<280 or control_state['game_launched']:raise ValueError('Native control-state checks failed')
                run([directory/'wa2_native_camera_state_tests.exe',directory/'native-camera-state.json'],console,90)
                camera_state=json.loads((directory/'native-camera-state.json').read_text())
                if not camera_state['passed'] or camera_state['cases']<30 or camera_state['guards']<6 or camera_state['game_launched'] or camera_state['screen_access']:raise ValueError('Native camera/background checks failed')
                run([directory/'wa2_native_voice_state_tests.exe',directory/'native-voice-state.json'],console,90)
                voice_state=json.loads((directory/'native-voice-state.json').read_text())
                if not voice_state['passed'] or voice_state['cases']<17 or voice_state['maximum_voice_slots']!=4 or voice_state['audio_devices_opened'] or voice_state['screen_access']:raise ValueError('Native speech state checks failed')
                run([directory/'wa2_native_novel_state_tests.exe',directory/'native-novel-state.json'],console,90)
                novel_state=json.loads((directory/'native-novel-state.json').read_text())
                if not novel_state['passed'] or novel_state['cases']<10 or novel_state['guards']<5 or novel_state['game_launched'] or novel_state['screen_access']:raise ValueError('Native novel state checks failed')
                run([directory/'wa2_native_weather_state_tests.exe',directory/'native-weather-state.json'],console,90)
                weather_state=json.loads((directory/'native-weather-state.json').read_text())
                if not weather_state['passed'] or weather_state['cases']<25 or weather_state['guards']<3 or weather_state['game_launched'] or weather_state['screen_access']:raise ValueError('Native weather state checks failed')
                run([directory/'wa2_native_calendar_state_tests.exe',directory/'native-calendar-state.json'],console,90)
                calendar_state=json.loads((directory/'native-calendar-state.json').read_text())
                if not calendar_state['passed'] or calendar_state['cases']<11 or calendar_state['guards']<5 or calendar_state['normal_ticks']!=182 or calendar_state['skip_ticks']!=62 or calendar_state['game_launched'] or calendar_state['screen_access']:raise ValueError('Native calendar state checks failed')
                run([directory/'wa2_native_animation_state_tests.exe',directory/'native-animation-state.json'],console,90)
                animation_state=json.loads((directory/'native-animation-state.json').read_text())
                if not animation_state['passed'] or animation_state['cases']<19 or animation_state['guards']<19 or animation_state['game_launched'] or animation_state['screen_access']:raise ValueError('Native animation state checks failed')
                run([directory/'wa2_native_scene_geometry_tests.exe',directory/'native-scene-geometry.json'],console,90)
                geometry_state=json.loads((directory/'native-scene-geometry.json').read_text())
                if not geometry_state['passed'] or geometry_state['cases']<30 or geometry_state['guards']<7 or geometry_state['devices_opened'] or geometry_state['game_launched'] or geometry_state['screen_access']:raise ValueError('Native scene geometry checks failed')
                run([directory/'wa2_native_shake_state_tests.exe',directory/'native-shake-state.json'],console,90)
                shake_state=json.loads((directory/'native-shake-state.json').read_text())
                if not shake_state['passed'] or shake_state['cases']<43 or shake_state['guards']<10 or shake_state['game_launched'] or shake_state['screen_access']:raise ValueError('Native shake checks failed')
                run([directory/'wa2_native_world_state_tests.exe',directory/'native-world-state.json'],console,90)
                world_state=json.loads((directory/'native-world-state.json').read_text())
                if not world_state['passed'] or world_state['cases']<35 or world_state['guards']<1 or world_state['devices_opened'] or world_state['game_launched'] or world_state['screen_access']:raise ValueError('Native world state checks failed')
                run([directory/'wa2_native_image_style_tests.exe',directory/'native-image-styles.json'],console,90)
                image_styles=json.loads((directory/'native-image-styles.json').read_text())
                if not image_styles['passed'] or image_styles['cases']<23 or image_styles['guards']<23 or image_styles['devices_opened'] or image_styles['game_launched'] or image_styles['screen_access']:raise ValueError('Native image style checks failed')
                run([directory/'wa2_native_caption_state_tests.exe',directory/'native-caption-state.json'],console,90)
                caption_state=json.loads((directory/'native-caption-state.json').read_text())
                if not caption_state['passed'] or caption_state['cases']<60 or caption_state['guards']<4 or caption_state['embedded_tracks']!=46 or not caption_state['synthetic_audio_only'] or caption_state['devices_opened'] or caption_state['game_launched'] or caption_state['screen_access']:raise ValueError('Native caption state checks failed')
                run([directory/'wa2_native_selection_state_tests.exe',directory/'native-selection-state.json'],console,90)
                selection_state=json.loads((directory/'native-selection-state.json').read_text())
                if not selection_state['passed'] or selection_state['cases']<17 or selection_state['guards']<5 or not selection_state['font_preparation_stubbed'] or selection_state['devices_opened'] or selection_state['game_launched'] or selection_state['screen_access']:raise ValueError('Native selection state checks failed')
                run([directory/'wa2_native_system_sound_tests.exe',directory/'native-system-sounds.json'],console,90)
                system_sound_state=json.loads((directory/'native-system-sounds.json').read_text())
                if not system_sound_state['passed'] or system_sound_state['cases']<15 or system_sound_state['guards']<12 or system_sound_state['cue_buffers']!=5 or system_sound_state['playback_resource_reads'] or not system_sound_state['synthetic_pcm_only'] or system_sound_state['devices_opened'] or system_sound_state['game_launched'] or system_sound_state['screen_access']:raise ValueError('Native system sound checks failed')
                run([directory/'wa2_native_lifecycle_state_tests.exe',directory/'native-lifecycle-state.json'],console,90)
                lifecycle_state=json.loads((directory/'native-lifecycle-state.json').read_text())
                if not lifecycle_state['passed'] or lifecycle_state['cases']<17 or lifecycle_state['guards']<3 or lifecycle_state['suspended_pcm_consumption'] or not lifecycle_state['synthetic_pcm_only'] or lifecycle_state['devices_opened'] or lifecycle_state['game_launched'] or lifecycle_state['screen_access']:raise ValueError('Native lifecycle state checks failed')
                run([directory/'wa2_native_stream_voice_tests.exe',directory/'native-stream-voice.json'],console,90)
                stream_voice_state=json.loads((directory/'native-stream-voice.json').read_text())
                if not stream_voice_state['passed'] or stream_voice_state['cases']<13 or stream_voice_state['guards']<1 or stream_voice_state['caller_source_accesses'] or not stream_voice_state['synthetic_pcm_only'] or stream_voice_state['devices_opened'] or stream_voice_state['game_launched'] or stream_voice_state['screen_access']:raise ValueError('Native asynchronous voice checks failed')
                run([directory/'wa2_native_dialogue_state_tests.exe',directory/'native-dialogue-state.json'],console,90)
                dialogue_state=json.loads((directory/'native-dialogue-state.json').read_text())
                if not dialogue_state['passed'] or dialogue_state['cases']<20 or dialogue_state['guards']<14 or dialogue_state['metadata_text_layouts']!=6 or dialogue_state['glyph_pixels_decoded'] or not dialogue_state['synthetic_only'] or dialogue_state['devices_opened'] or dialogue_state['game_launched'] or dialogue_state['screen_access']:raise ValueError('Native dialogue state checks failed')
                run([directory/'wa2_native_sprite_atlas_tests.exe',directory/'native-sprite-atlas.json'],console,90)
                sprite_atlas_state=json.loads((directory/'native-sprite-atlas.json').read_text())
                if not sprite_atlas_state['passed'] or sprite_atlas_state['cases']<9 or sprite_atlas_state['guards']<5 or not sprite_atlas_state['replicated_gutters_verified'] or not sprite_atlas_state['input_pixels_unchanged'] or sprite_atlas_state['devices_opened'] or sprite_atlas_state['game_launched'] or sprite_atlas_state['screen_access']:raise ValueError('Native sprite atlas checks failed')
                run([directory/'wa2_native_font_coverage_tests.exe',directory/'native-font-coverage.json'],console,90)
                font_coverage_state=json.loads((directory/'native-font-coverage.json').read_text())
                if not font_coverage_state['passed'] or font_coverage_state['cases']<5 or font_coverage_state['guards']<7 or not font_coverage_state['matches_rgba_region_blue'] or not font_coverage_state['palette_alpha_ignored'] or font_coverage_state['devices_opened'] or font_coverage_state['game_launched'] or font_coverage_state['screen_access']:raise ValueError('Native font coverage checks failed')
                run([directory/'wa2_native_frame_batch_tests.exe',directory/'native-frame-batch.json'],console,90)
                frame_batch_state=json.loads((directory/'native-frame-batch.json').read_text())
                if not frame_batch_state['passed'] or frame_batch_state['cases']<9 or frame_batch_state['guards']<6 or not frame_batch_state['transient_input_once'] or not frame_batch_state['bounded_on_constant_clock'] or not frame_batch_state['fake_steps_and_clock_only'] or frame_batch_state['scene_instances_created'] or frame_batch_state['devices_opened'] or frame_batch_state['game_launched'] or frame_batch_state['screen_access']:raise ValueError('Native frame batch checks failed')
                run([directory/'wa2_native_backlog_state_tests.exe',directory/'native-backlog-state.json'],console,90)
                backlog_state=json.loads((directory/'native-backlog-state.json').read_text())
                if not backlog_state['passed'] or backlog_state['cases']<10 or backlog_state['guards']<11 or backlog_state['entry_capacity']!=255 or backlog_state['maximum_entry_voices']!=10 or not backlog_state['synthetic_only'] or backlog_state['devices_opened'] or backlog_state['game_launched'] or backlog_state['screen_access']:raise ValueError('Native backlog state checks failed')
                shader_directory=directory/'mask-shaders'
                run([directory/'wa2_native_mask_source_tests.exe',shader_directory],console,90)
                for profile in ('desktop','embedded'):
                    vertex=shader_directory/('native-mask-'+profile+'.vert');fragment=shader_directory/('native-mask-'+profile+'.frag')
                    run([args.shader_compiler.resolve(),'-l',vertex,fragment],console,90)
                    mask_shader_checks.append(dict(profile=profile,vertex_sha256=digest(vertex),fragment_sha256=digest(fragment),offline_compile_and_link=True))
                if args.sdl_archive:
                    run([directory/'wa2_native_scene_composition_tests.exe',directory/'native-scene-composition.json'],console,90)
                    composition_state=json.loads((directory/'native-scene-composition.json').read_text())
                    if not composition_state['passed'] or composition_state['cases']<26 or not composition_state['owned_memory_only'] or composition_state['windows_created'] or composition_state['devices_opened'] or composition_state['game_launched'] or composition_state['screen_access']:raise ValueError('Native owned-memory composition checks failed')
                    composition_graph=output([ninja,'-C',directory,'-t','graph','wa2_native_scene_composition_tests.exe']).replace('\\','/').lower()
                    if any(token in composition_graph for token in forbidden):raise ValueError('AOT dependency in owned-memory composition test')
                    composition_artifact=dict(sha256=digest(directory/'wa2_native_scene_composition_tests.exe'),
                        native_link_graph_sha256=hashlib.sha256(composition_graph.encode()).hexdigest())
                if args.resource_root:
                    run([directory/'wa2_native_script_catalogue_tests.exe',args.resource_root.resolve(),directory/'native-script-catalogue.json'],console,90)
                    catalogue=json.loads((directory/'native-script-catalogue.json').read_text())
                    if not catalogue['passed'] or catalogue['calendar_atlas_headers']!=4 or catalogue['selection_atlas_headers']!=2 or catalogue['colour_maps_parsed']!=2 or not catalogue['sprite_animations'] or not catalogue['animation_frames'] or catalogue['animation_sequences_executed'] or catalogue['script_executed'] or catalogue['game_launched']:raise ValueError('Invalid static resource script audit')
    if sources['windows']!=sources['android']:raise ValueError('Native game sources differ between host platforms')
    for name,sha in bindings.items():
        if digest(ROOT/name)!=sha:raise ValueError('Native verification source changed: '+name)
    if args.sdl_archive and digest(args.sdl_archive)!=composition_sdl_sha:raise ValueError('SDL archive changed during owned-memory verification')
    if digest(args.shader_compiler)!=shader_compiler_sha:raise ValueError('Offline shader compiler changed during verification')
    report=dict(schema='wa2.native-core.v1',source_sha256=bindings,codec_archives_sha256=codec_bindings,
        native_compiled_sources=sources['windows'],same_native_game_sources=True,artifacts=artifacts,progress=checks,script_state=script_state,control_state=control_state,camera_state=camera_state,voice_state=voice_state,novel_state=novel_state,weather_state=weather_state,calendar_state=calendar_state,animation_state=animation_state,geometry_state=geometry_state,shake_state=shake_state,world_state=world_state,image_styles=image_styles,caption_state=caption_state,selection_state=selection_state,composition_state=composition_state,composition_artifact=composition_artifact,composition_sdl_archive_sha256=composition_sdl_sha,mask_shader_checks=mask_shader_checks,shader_compiler_sha256=shader_compiler_sha,presentation_adapter_compiled=True,presentation_adapter_linked=composition_state is not None,resource_script_catalogue=catalogue,
        recompiled_game_linked=False,guest_cpu_and_memory_linked=False,source_executable_opened=False,screen_access=False,
        game_launched=False,emulator_launched=False,android_device_verified=False,complete_gameplay_verified=False,
        native_engine_is_complete=False,apk_created=False,current_release_replaced=False,maximum_build_jobs=1,passed=True)
    report['system_sound_state']=system_sound_state
    report['lifecycle_state']=lifecycle_state
    report['stream_voice_state']=stream_voice_state
    report['dialogue_state']=dialogue_state
    report['sprite_atlas_state']=sprite_atlas_state
    report['font_coverage_state']=font_coverage_state
    report['frame_batch_state']=frame_batch_state
    report['backlog_state']=backlog_state
    (ROOT/'logs/native-core-verification.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PASS shared native core: '+str(len(sources['windows']))+' identical common sources, full link on x64/ARM64, no AOT dependency. This is not a complete game.',flush=True)


if __name__=='__main__':main()
