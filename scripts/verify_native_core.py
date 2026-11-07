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
    parser.add_argument('--android-sdl-library',type=Path,help='Optional existing Android SDL2 library for a native JNI/SDL entry link check; never loaded')
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
    inputs += [ROOT/'tests/native_system_sounds.cpp',ROOT/'tests/native_lifecycle_state.cpp',ROOT/'tests/native_stream_voice.cpp',ROOT/'tests/native_dialogue_state.cpp',ROOT/'tests/native_sprite_atlas.cpp',ROOT/'tests/native_font_coverage.cpp',ROOT/'tests/native_frame_batch.cpp',ROOT/'tests/native_backlog_state.cpp',ROOT/'tests/native_background_state.cpp',ROOT/'tests/native_character_state.cpp']
    inputs += [ROOT/'tests/native_bitmap_state.cpp']
    inputs += [ROOT/'tests/native_weather_checkpoint.cpp']
    inputs += [ROOT/'tests/native_audio_checkpoint.cpp']
    inputs += [ROOT/'tests/native_saved_scene.cpp',ROOT/'tests/native_scene_checkpoint.cpp']
    inputs += [ROOT/'tests/native_save_slots.cpp',ROOT/'platform/save_storage.cpp',ROOT/'platform/save_storage.hpp']
    inputs += [ROOT/'tests/native_save_document.cpp']
    inputs += [ROOT/'tests/native_save_view.cpp',ROOT/'tests/native_save_atlases.hpp']
    inputs += [ROOT/'tests/native_save_application.cpp']
    inputs += [ROOT/'tests/native_backlog_view.cpp']
    inputs += [ROOT/'tests/native_music_view.cpp']
    inputs += [ROOT/'tests/native_extra_grid.cpp']
    inputs += [ROOT/p for p in ('platform/native_user_menu.hpp','platform/sdl_user_command.hpp','android/game/user_menu_jni.hpp','android/game/user_menu_jni.cpp','hosts/native-android/CMakeLists.txt')]
    inputs += [ROOT/p for p in ('tests/native_desktop.cpp','platform/native_desktop.hpp','platform/native_desktop.cpp',
        'platform/native_host.hpp','platform/native_host.cpp','platform/native_host.cmake','hosts/native/CMakeLists.txt',
        'hosts/native/entry.cmake','hosts/native/windows_main.cpp','tools/SDL-release-2.32.10/src/main/windows/SDL_windows_main.c')]
    inputs += [ROOT/'tests/native_preferences.cpp']
    inputs += [ROOT/'tests/native_application.cpp',ROOT/'tests/native_scene_fixture.hpp']
    inputs += [ROOT/'tests/native_title_view.cpp']
    inputs += [ROOT/'tests/native_options_view.cpp']
    inputs += [ROOT/'tests/native_options_preferences.cpp']
    inputs += [ROOT/'tests/native_options_application.cpp',ROOT/'tests/native_options_atlases.hpp']
    inputs += [ROOT/'tests/native_options_actions.cpp']
    inputs += [ROOT/'tests/native_title_scene.cpp']
    inputs += [ROOT/'tests/native_render_frame.cpp',ROOT/'platform/sdl_movie.cpp',ROOT/'platform/sdl_movie.hpp',ROOT/'platform/sdl_presentation.cpp',ROOT/'platform/sdl_presentation.hpp']
    inputs += [ROOT/'tests/native_movie_state.cpp']
    inputs += [ROOT/'tests/native_frame_driver.cpp',ROOT/'tests/native_audio_binding.cpp',ROOT/'tests/native_sdl_application.cpp',ROOT/'tests/native_input.cpp',
        ROOT/'platform/audio_output.hpp',ROOT/'platform/audio_binding.hpp',ROOT/'platform/audio_binding.cpp',ROOT/'platform/sdl_audio.hpp',ROOT/'platform/sdl_audio.cpp',
        ROOT/'platform/sdl_input.hpp',ROOT/'platform/sdl_input.cpp',ROOT/'platform/sdl_application.hpp',ROOT/'platform/sdl_application.cpp']
    inputs += [ROOT/'tests/native_image_wait.cpp',ROOT/'tests/native_bootstrap.cpp',ROOT/'platform/native_application.hpp',ROOT/'platform/native_application.cpp',ROOT/'platform/native_services.cmake',
        ROOT/'android/game/game_host.cpp',ROOT/'android/game/CMakeLists.txt',ROOT/'android/native/android_host.hpp',ROOT/'android/native/android_resources.hpp']
    inputs += [ROOT/p for p in ('tests/android_jni_services.cpp','android/game/entry.cmake','android/game/game_jni.cpp',
        'android/native/service_lifecycle.hpp','android/native/frame_hud_jni.hpp','android/native/frame_hud_jni.cpp',
        'android/native/frame_metrics.hpp','android/native/frame_metrics.cpp','android/native/frame_metrics_jni.cpp',
        'android/native/android_entry.cpp','android/native/android_resources.cpp','android/native/resource_jni.hpp',
        'android/native/widgets_jni.hpp','android/native/shell_jni.hpp','platform/resource_fd.hpp','platform/resource_fd.cpp',
        'hosts/CMakeLists.txt')]
    inputs += [ROOT/'tests/native_preference_storage.cpp',ROOT/'platform/private_file.cpp',ROOT/'platform/private_file.hpp',ROOT/'platform/preference_storage.cpp',ROOT/'platform/preference_storage.hpp']
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
    sources={};artifacts={};codec_bindings={};composition_state=None;composition_artifact=None;sdl_application=None;input_state=None
    composition_sdl_sha=digest(args.sdl_archive) if args.sdl_archive else None
    android_sdl_sha=digest(args.android_sdl_library) if args.android_sdl_library else None
    android_entry_artifact=None;windows_entry_artifact=None
    shader_compiler_sha=digest(args.shader_compiler);mask_shader_checks=[]
    with (ROOT/'logs/native-core-build.log').open('wb') as console:
        for host in ('windows','android'):
            directory=build/host
            configure=[cmake,'-S',ROOT/'native','-B',directory,'-G','Ninja','-DCMAKE_BUILD_TYPE=MinSizeRel',
                '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON','-DCMAKE_MAKE_PROGRAM='+str(ninja)]
            if host=='windows':configure+=['-DCMAKE_TOOLCHAIN_FILE='+str(ROOT/'scripts/zig-toolchain.cmake'),'-DWA2_NATIVE_SDL_ARCHIVE='+(str(args.sdl_archive.resolve()) if args.sdl_archive else '')]
            else:configure+=['-DCMAKE_TOOLCHAIN_FILE='+str(ndk/'build/cmake/android.toolchain.cmake'),'-DANDROID_ABI=arm64-v8a','-DANDROID_PLATFORM=android-26','-DANDROID_STL=c++_shared',
                '-DWA2_NATIVE_ANDROID_SDL_LIBRARY='+(str(args.android_sdl_library.resolve()) if args.android_sdl_library else '')]
            print('Configuring and building '+host+' native core; one hidden low-priority job.',flush=True)
            run(configure,console)
            targets=['wa2_native_link_check','wa2_native_presentation_check']+(['wa2_native_progress_tests','wa2_native_script_state_tests','wa2_native_control_state_tests','wa2_native_camera_state_tests','wa2_native_voice_state_tests','wa2_native_novel_state_tests','wa2_native_weather_state_tests','wa2_native_calendar_state_tests','wa2_native_animation_state_tests','wa2_native_scene_geometry_tests','wa2_native_shake_state_tests','wa2_native_world_state_tests','wa2_native_image_style_tests','wa2_native_caption_state_tests','wa2_native_selection_state_tests'] if host=='windows' else [])
            if host=='windows':targets+=['wa2_native_system_sound_tests','wa2_native_lifecycle_state_tests','wa2_native_stream_voice_tests','wa2_native_dialogue_state_tests','wa2_native_sprite_atlas_tests','wa2_native_font_coverage_tests','wa2_native_frame_batch_tests','wa2_native_backlog_state_tests','wa2_native_background_state_tests','wa2_native_character_state_tests']
            if host=='windows':targets+=['wa2_native_bitmap_state_tests']
            if host=='windows':targets+=['wa2_native_weather_checkpoint_tests']
            if host=='windows':targets+=['wa2_native_audio_checkpoint_tests']
            if host=='windows':targets+=['wa2_native_saved_scene_tests','wa2_native_scene_checkpoint_tests']
            if host=='windows':targets+=['wa2_native_save_slot_tests']
            if host=='windows':targets+=['wa2_native_preference_tests']
            if host=='windows':targets+=['wa2_native_preference_storage_tests']
            if host=='windows':targets+=['wa2_native_application_tests']
            if host=='windows':targets+=['wa2_native_title_view_tests']
            if host=='windows':targets+=['wa2_native_options_view_tests']
            if host=='windows':targets+=['wa2_native_options_preference_tests']
            if host=='windows':targets+=['wa2_native_options_application_tests']
            if host=='windows':targets+=['wa2_native_options_action_tests']
            if host=='windows':targets+=['wa2_native_save_document_tests']
            if host=='windows':targets+=['wa2_native_save_view_tests','wa2_native_save_application_tests']
            if host=='windows':targets+=['wa2_native_backlog_view_tests']
            if host=='windows':targets+=['wa2_native_music_view_tests']
            if host=='windows':targets+=['wa2_native_extra_grid_tests']
            if host=='windows':targets+=['wa2_native_title_scene_tests']
            if host=='windows':targets+=['wa2_native_render_frame_tests']
            if host=='windows':targets+=['wa2_native_movie_state_tests']
            if host=='windows':targets+=['wa2_native_frame_driver_tests','wa2_native_audio_binding_tests']
            if host=='windows':targets+=['wa2_native_image_wait_tests','wa2_native_bootstrap_tests','wa2_android_jni_service_tests']
            if host=='windows':targets+=['wa2_native_desktop_tests']
            if host=='android':targets+=['wa2_native_android_host_check','wa2_retained_android_jni_check']
            if host=='android' and args.android_sdl_library:targets+=['wa2_native_android_entry_check']
            if host=='windows' and args.resource_root:targets.append('wa2_native_script_catalogue_tests')
            if host=='windows' and args.sdl_archive:targets.append('wa2_native_scene_composition_tests')
            if host=='windows' and args.sdl_archive:targets+=['wa2_native_sdl_application_tests','wa2_native_input_tests']
            if host=='windows' and args.sdl_archive:targets+=['wa2_native_windows_entry_check']
            if host=='windows':targets.append('wa2_native_mask_source_tests')
            run([cmake,'--build',directory,'--target',*targets,'--parallel','1'],console,2400)
            commands=json.loads((directory/'compile_commands.json').read_text())
            names={Path(item['file']).resolve().relative_to(ROOT).as_posix() for item in commands}
            allowed=('engine/','game/','platform/progress_storage.cpp','platform/file_io.cpp','platform/sdl_scene.cpp','platform/sdl_canvas.cpp','platform/sdl_mask.cpp','platform/mask_shaders.cpp','tests/native_core_link.cpp','tests/native_progress.cpp','tests/native_script_state.cpp','tests/native_control_state.cpp','tests/native_camera_state.cpp','tests/native_script_catalogue.cpp','tests/native_voices.cpp','tests/native_novel_state.cpp','tests/native_weather_state.cpp','tests/native_calendar_state.cpp','tests/native_animation_state.cpp','tests/native_scene_geometry.cpp','tests/native_shake_state.cpp','tests/native_world_state.cpp','tests/native_scene_composition.cpp','tests/native_image_styles.cpp','tests/native_caption_state.cpp','tests/native_selection_state.cpp','tests/native_mask_sources.cpp','tests/pcm_audio.cpp')
            allowed+=('tests/native_system_sounds.cpp','tests/native_lifecycle_state.cpp','tests/native_stream_voice.cpp','tests/native_dialogue_state.cpp','tests/native_sprite_atlas.cpp','tests/native_font_coverage.cpp','tests/native_frame_batch.cpp','tests/native_backlog_state.cpp','tests/native_background_state.cpp','tests/native_character_state.cpp')
            allowed+=('tests/native_bitmap_state.cpp',)
            allowed+=('tests/native_weather_checkpoint.cpp',)
            allowed+=('tests/native_audio_checkpoint.cpp',)
            allowed+=('tests/native_saved_scene.cpp','tests/native_scene_checkpoint.cpp')
            allowed+=('tests/native_save_slots.cpp','platform/save_storage.cpp')
            allowed+=('tests/native_preferences.cpp',)
            allowed+=('tests/native_preference_storage.cpp','platform/private_file.cpp','platform/preference_storage.cpp')
            allowed+=('tests/native_application.cpp',)
            allowed+=('tests/native_title_view.cpp',)
            allowed+=('tests/native_options_view.cpp',)
            allowed+=('tests/native_options_preferences.cpp',)
            allowed+=('tests/native_options_application.cpp',)
            allowed+=('tests/native_options_actions.cpp',)
            allowed+=('tests/native_save_document.cpp',)
            allowed+=('tests/native_save_view.cpp',)
            allowed+=('tests/native_save_application.cpp',)
            allowed+=('tests/native_backlog_view.cpp',)
            allowed+=('tests/native_music_view.cpp',)
            allowed+=('tests/native_extra_grid.cpp',)
            allowed+=('tests/native_desktop.cpp','platform/native_desktop.cpp','platform/native_host.cpp',
                'hosts/native/windows_main.cpp','tools/SDL-release-2.32.10/src/main/windows/SDL_windows_main.c')
            allowed+=('tests/native_title_scene.cpp',)
            allowed+=('tests/native_render_frame.cpp','platform/sdl_movie.cpp','platform/sdl_presentation.cpp')
            allowed+=('tests/native_movie_state.cpp',)
            allowed+=('tests/native_frame_driver.cpp','tests/native_audio_binding.cpp','tests/native_sdl_application.cpp','tests/native_input.cpp',
                'platform/audio_binding.cpp','platform/sdl_audio.cpp','platform/sdl_input.cpp','platform/sdl_application.cpp')
            allowed+=('tests/native_image_wait.cpp','tests/native_bootstrap.cpp','platform/native_application.cpp')
            allowed+=('tests/android_jni_services.cpp',)
            if host=='android':allowed+=('android/game/game_host.cpp','android/game/game_jni.cpp','android/game/user_menu_jni.cpp','android/native/android_entry.cpp',
                'android/native/android_resources.cpp','android/native/frame_metrics.cpp','android/native/frame_hud_jni.cpp',
                'android/native/frame_metrics_jni.cpp','platform/resource_fd.cpp')
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
                if name.startswith(('engine/','game/','platform/progress_storage.cpp','platform/save_storage.cpp','platform/private_file.cpp','platform/preference_storage.cpp')):linked_sources.append(name)
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
                if args.android_sdl_library:
                    entry=directory/'libwa2_native_android_entry_check.so'
                    entry_graph=output([ninja,'-C',directory,'-t','graph',entry.name]).replace('\\','/').lower()
                    if any(token in entry_graph for token in forbidden):raise ValueError('Recompilation dependency in native Android entry link graph')
                    entry_symbols=output([llvm/'llvm-nm.exe','--defined-only','--demangle',entry])
                    for token in ('wr::','wa2::aot::','code_page_','native_backend_','initialize_widgets_jni','initialize_shell_jni','mount_compiled_resources'):
                        if token in entry_symbols:raise ValueError('Legacy runtime service in native Android entry: '+token)
                    for token in ('JNI_OnLoad','JNI_OnUnload','SDL_main','Java_org_wa2_port_Wa2Activity_nativeResetFrameMetrics',
                            'wa2::android::run_game(','wa2::android::frame_presented(','wa2::android::open_resource_bank(',
                            'wa2::platform::SdlApplication::run(','wa2::platform::SdlAudio::open(','wa2::game::Application::frame(',
                            'wa2::game::SaveView::update(','wa2::platform::save_thumbnail(','wa2::game::BacklogView::update(',
                            'wa2::android::initialize_user_menu_jni(','wa2::android::publish_user_menu(','wa2::game::Application::user_command('):
                        if token not in entry_symbols:raise ValueError('Missing native Android entry service: '+token)
                    for name in (b'org/wa2/port/PortUi',b'nativeLabels',b'nativeState',b'nativeChoose'):
                        if name not in entry.read_bytes():raise ValueError('Missing native Android menu JNI registration literal: '+str(name))
                    entry_header=output([llvm/'llvm-readelf.exe','-h',entry])
                    if 'AArch64' not in entry_header:raise ValueError('Native Android entry has wrong architecture')
                    entry_dynamic=output([llvm/'llvm-readelf.exe','-d',entry])
                    if 'libSDL2.so' not in entry_dynamic or any(name in entry_dynamic for name in ('libwa2_android.so','libwinrecomp')):
                        raise ValueError('Invalid native Android entry dynamic dependencies')
                    entry_sources=sorted({Path(item['file']).resolve().relative_to(ROOT).as_posix() for item in commands
                        if Path(item['output']).resolve().relative_to(directory.resolve()).as_posix().lower() in entry_graph})
                    if not set(sources[host]).issubset(entry_sources):raise ValueError('Native Android entry does not use the complete common core')
                    android_entry_artifact=dict(file=str(entry),sha256=digest(entry),bytes=entry.stat().st_size,
                        native_link_graph_sha256=hashlib.sha256(entry_graph.encode()).hexdigest(),compiled_sources=entry_sources,
                        symbol_and_dynamic_dependencies_audited=True,library_loaded=False,apk_created=False)
            artifacts[host]=dict(file=str(library),sha256=digest(library),bytes=library.stat().st_size,
                native_archives_sha256={p.relative_to(directory).as_posix():digest(p) for p in archives},
                symbol_audit_scope='PE exports and link-input archives' if host=='windows' else 'linked ELF symbols',
                compile_commands_sha256=digest(directory/'compile_commands.json'),native_link_graph_sha256=hashlib.sha256(graph.encode()).hexdigest())
            if host=='windows' and args.sdl_archive:
                entry=directory/'wa2_native_windows_entry_check.exe';entry_graph=output([ninja,'-C',directory,'-t','graph',entry.name]).replace('\\','/').lower()
                if any(token in entry_graph for token in forbidden):raise ValueError('Recompilation dependency in native Windows entry')
                entry_exports=output([llvm/'llvm-readobj.exe','--coff-exports',entry])
                for token in ('SDL_main','run_native_host'):
                    if token not in entry_exports:raise ValueError('Missing actual native Windows entry export: '+token)
                host_archive=directory/'libwa2_native_host.a';host_symbols=output([llvm/'llvm-nm.exe','--defined-only','--demangle',host_archive])
                for token in ('run_native_host','SdlApplication::run'):
                    if token not in host_symbols:raise ValueError('Missing native Windows host loop symbol: '+token)
                for token in ('wr::','wa2::aot::','code_page_','native_backend_'):
                    if token in entry_exports or token in host_symbols:raise ValueError('Recompilation symbol in native Windows entry')
                entry_header=output([llvm/'llvm-readobj.exe','--file-headers','--coff-imports',entry])
                if 'IMAGE_FILE_MACHINE_AMD64' not in entry_header or 'IMAGE_SUBSYSTEM_WINDOWS_GUI' not in entry_header:raise ValueError('Native Windows entry is not a GUI x64 executable')
                if any(token in entry_header.lower() for token in ('libwinrecomp','wa2_android','wa2_aot')):raise ValueError('Legacy DLL imported by native Windows entry')
                entry_sources=sorted({Path(item['file']).resolve().relative_to(ROOT).as_posix() for item in commands
                    if Path(item['output']).resolve().relative_to(directory.resolve()).as_posix().lower() in entry_graph})
                if not set(sources[host]).issubset(entry_sources):raise ValueError('Native Windows entry does not use the complete common core')
                windows_entry_artifact=dict(file=str(entry),sha256=digest(entry),bytes=entry.stat().st_size,
                    native_host_archive_sha256=digest(host_archive),compiled_sources=entry_sources,symbol_audit_scope='PE entry exports and linked native-host archive',
                    native_link_graph_sha256=hashlib.sha256(entry_graph.encode()).hexdigest(),actual_entry_symbols_audited=True,
                    memory_reference_sdl_only=True,executable_started=False,released_game=False)
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
                if not novel_state['passed'] or novel_state['cases']<11 or novel_state['guards']<5 or novel_state['game_launched'] or novel_state['screen_access']:raise ValueError('Native novel state checks failed')
                run([directory/'wa2_native_weather_state_tests.exe',directory/'native-weather-state.json'],console,90)
                weather_state=json.loads((directory/'native-weather-state.json').read_text())
                if not weather_state['passed'] or weather_state['cases']<28 or weather_state['guards']<3 or weather_state['game_launched'] or weather_state['screen_access']:raise ValueError('Native weather state checks failed')
                run([directory/'wa2_native_calendar_state_tests.exe',directory/'native-calendar-state.json'],console,90)
                calendar_state=json.loads((directory/'native-calendar-state.json').read_text())
                if not calendar_state['passed'] or calendar_state['cases']<11 or calendar_state['guards']<5 or calendar_state['normal_ticks']!=182 or calendar_state['skip_ticks']!=62 or calendar_state['game_launched'] or calendar_state['screen_access']:raise ValueError('Native calendar state checks failed')
                run([directory/'wa2_native_animation_state_tests.exe',directory/'native-animation-state.json'],console,90)
                animation_state=json.loads((directory/'native-animation-state.json').read_text())
                if not animation_state['passed'] or animation_state['cases']<20 or animation_state['guards']<19 or animation_state['game_launched'] or animation_state['screen_access']:raise ValueError('Native animation state checks failed')
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
                if not image_styles['passed'] or image_styles['cases']<27 or image_styles['guards']<27 or image_styles['devices_opened'] or image_styles['game_launched'] or image_styles['screen_access']:raise ValueError('Native image style checks failed')
                run([directory/'wa2_native_caption_state_tests.exe',directory/'native-caption-state.json'],console,90)
                caption_state=json.loads((directory/'native-caption-state.json').read_text())
                if not caption_state['passed'] or caption_state['cases']<60 or caption_state['guards']<4 or caption_state['embedded_tracks']!=46 or not caption_state['synthetic_audio_only'] or caption_state['devices_opened'] or caption_state['game_launched'] or caption_state['screen_access']:raise ValueError('Native caption state checks failed')
                run([directory/'wa2_native_selection_state_tests.exe',directory/'native-selection-state.json'],console,90)
                selection_state=json.loads((directory/'native-selection-state.json').read_text())
                if not selection_state['passed'] or selection_state['cases']<25 or selection_state['guards']<15 or not selection_state['font_preparation_stubbed'] or selection_state['devices_opened'] or selection_state['game_launched'] or selection_state['screen_access']:raise ValueError('Native selection state checks failed')
                run([directory/'wa2_native_system_sound_tests.exe',directory/'native-system-sounds.json'],console,90)
                system_sound_state=json.loads((directory/'native-system-sounds.json').read_text())
                if not system_sound_state['passed'] or system_sound_state['cases']<15 or system_sound_state['guards']<12 or system_sound_state['cue_buffers']!=5 or system_sound_state['playback_resource_reads'] or not system_sound_state['synthetic_pcm_only'] or system_sound_state['devices_opened'] or system_sound_state['game_launched'] or system_sound_state['screen_access']:raise ValueError('Native system sound checks failed')
                run([directory/'wa2_native_lifecycle_state_tests.exe',directory/'native-lifecycle-state.json'],console,90)
                lifecycle_state=json.loads((directory/'native-lifecycle-state.json').read_text())
                if not lifecycle_state['passed'] or lifecycle_state['cases']<20 or lifecycle_state['guards']<6 or lifecycle_state['suspended_pcm_consumption'] or not lifecycle_state['synthetic_pcm_only'] or lifecycle_state['devices_opened'] or lifecycle_state['game_launched'] or lifecycle_state['screen_access']:raise ValueError('Native lifecycle state checks failed')
                run([directory/'wa2_native_stream_voice_tests.exe',directory/'native-stream-voice.json'],console,90)
                stream_voice_state=json.loads((directory/'native-stream-voice.json').read_text())
                if not stream_voice_state['passed'] or stream_voice_state['cases']<13 or stream_voice_state['guards']<1 or stream_voice_state['caller_source_accesses'] or not stream_voice_state['synthetic_pcm_only'] or stream_voice_state['devices_opened'] or stream_voice_state['game_launched'] or stream_voice_state['screen_access']:raise ValueError('Native asynchronous voice checks failed')
                run([directory/'wa2_native_dialogue_state_tests.exe',directory/'native-dialogue-state.json'],console,90)
                dialogue_state=json.loads((directory/'native-dialogue-state.json').read_text())
                if not dialogue_state['passed'] or dialogue_state['cases']<20 or dialogue_state['guards']<16 or dialogue_state['metadata_text_layouts']!=6 or dialogue_state['glyph_pixels_decoded'] or not dialogue_state['synthetic_only'] or dialogue_state['devices_opened'] or dialogue_state['game_launched'] or dialogue_state['screen_access']:raise ValueError('Native dialogue state checks failed')
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
                run([directory/'wa2_native_background_state_tests.exe',directory/'native-background-state.json'],console,90)
                background_state=json.loads((directory/'native-background-state.json').read_text())
                if not background_state['passed'] or background_state['cases']<17 or background_state['guards']<11 or not background_state['external_image_references'] or not background_state['restore_callbacks_not_replayed'] or not background_state['synthetic_only'] or background_state['devices_opened'] or background_state['game_launched'] or background_state['screen_access']:raise ValueError('Native background state checks failed')
                run([directory/'wa2_native_character_state_tests.exe',directory/'native-character-state.json'],console,90)
                character_state=json.loads((directory/'native-character-state.json').read_text())
                if not character_state['passed'] or character_state['cases']<13 or character_state['guards']<10 or not character_state['pending_style_preserved'] or not character_state['retired_layers_restored'] or not character_state['synthetic_only'] or character_state['devices_opened'] or character_state['game_launched'] or character_state['screen_access']:raise ValueError('Native character state checks failed')
                run([directory/'wa2_native_bitmap_state_tests.exe',directory/'native-bitmap-state.json'],console,90)
                bitmap_state=json.loads((directory/'native-bitmap-state.json').read_text())
                if not bitmap_state['passed'] or bitmap_state['cases']<24 or bitmap_state['guards']<14 or not bitmap_state['animation_progress_restored'] or not bitmap_state['pending_styles_preserved'] or not bitmap_state['retired_layers_restored'] or not bitmap_state['synthetic_only'] or bitmap_state['devices_opened'] or bitmap_state['game_launched'] or bitmap_state['screen_access']:raise ValueError('Native bitmap state checks failed')
                run([directory/'wa2_native_weather_checkpoint_tests.exe',directory/'native-weather-checkpoint.json'],console,90)
                weather_checkpoint=json.loads((directory/'native-weather-checkpoint.json').read_text())
                if not weather_checkpoint['passed'] or weather_checkpoint['cases']<18 or weather_checkpoint['guards']<13 or weather_checkpoint['restore_random_calls'] or not weather_checkpoint['same_frame_geometry'] or not weather_checkpoint['atlas_regenerated'] or not weather_checkpoint['synthetic_only'] or weather_checkpoint['devices_opened'] or weather_checkpoint['game_launched'] or weather_checkpoint['screen_access']:raise ValueError('Native weather checkpoint checks failed')
                run([directory/'wa2_native_audio_checkpoint_tests.exe',directory/'native-audio-checkpoint.json'],console,90)
                audio_checkpoint=json.loads((directory/'native-audio-checkpoint.json').read_text())
                if not audio_checkpoint['passed'] or audio_checkpoint['cases']<18 or audio_checkpoint['guards']<16 or audio_checkpoint['restore_history_events'] or not audio_checkpoint['looping_audio_restarted'] or not audio_checkpoint['one_shots_not_replayed'] or not audio_checkpoint['fake_voices_only'] or audio_checkpoint['devices_opened'] or audio_checkpoint['game_launched'] or audio_checkpoint['screen_access']:raise ValueError('Native audio checkpoint checks failed')
                run([directory/'wa2_native_saved_scene_tests.exe',directory/'native-saved-scene.json'],console,90)
                saved_scene=json.loads((directory/'native-saved-scene.json').read_text())
                if not saved_scene['passed'] or saved_scene['cases']<20 or saved_scene['guards']<35 or saved_scene['subsystem_envelopes']!=17 or not saved_scene['envelope_checks_only'] or saved_scene['scene_instances_created'] or saved_scene['original_resources_opened'] or saved_scene['devices_opened'] or saved_scene['game_launched'] or saved_scene['screen_access']:raise ValueError('Native scene save envelope checks failed')
                run([directory/'wa2_native_scene_checkpoint_tests.exe',directory/'native-scene-checkpoint.json'],console,90)
                scene_checkpoint=json.loads((directory/'native-scene-checkpoint.json').read_text())
                if not scene_checkpoint['passed'] or scene_checkpoint['cases']<7 or scene_checkpoint['guards']<7 or not scene_checkpoint['synthetic_script_only'] or not scene_checkpoint['independent_logical_clocks'] or not scene_checkpoint['old_scene_preserved_on_failure'] or scene_checkpoint['font_glyph_pixels_decoded'] or scene_checkpoint['original_resources_opened'] or scene_checkpoint['devices_opened'] or scene_checkpoint['game_launched'] or scene_checkpoint['screen_access']:raise ValueError('Synthetic native scene restore checks failed')
                run([directory/'wa2_native_save_slot_tests.exe',directory/'native-save-slots.json'],console,90)
                save_slots=json.loads((directory/'native-save-slots.json').read_text())
                if not save_slots['passed'] or save_slots['cases']<10 or save_slots['guards']<19 or save_slots['slot_capacity']!=1000 or not save_slots['synthetic_files_only'] or save_slots['original_saves_opened'] or save_slots['game_launched'] or save_slots['screen_access']:raise ValueError('Native save slot checks failed')
                run([directory/'wa2_native_save_document_tests.exe',directory/'native-save-document.json'],console,90)
                save_document=json.loads((directory/'native-save-document.json').read_text())
                if not save_document['passed'] or save_document['cases']<25 or save_document['guards']<42 or not save_document['bounded_preview_reads'] or not save_document['independent_preview_checksum'] or not save_document['legacy_snapshot_loads'] or not save_document['failed_write_preserves_slot'] or not save_document['crc_compatible'] or not save_document['synthetic_files_only'] or save_document['original_saves_opened'] or save_document['devices_opened'] or save_document['game_launched'] or save_document['screen_access']:raise ValueError('Native save document checks failed')
                run([directory/'wa2_native_save_view_tests.exe',directory/'native-save-view.json'],console,90)
                save_view=json.loads((directory/'native-save-view.json').read_text())
                if not save_view['passed'] or save_view['cases']<26 or save_view['guards']<24 or save_view['manual_slots']!=100 or not save_view['two_column_source_geometry'] or not save_view['source_bitmap_digit_grid'] or not save_view['overwrite_and_load_confirmations'] or save_view['maximum_preview_reads']>1 or save_view['maximum_pending_images']>1 or save_view['maximum_new_images_per_tick']>2 or not save_view['failed_operations_keep_rows'] or not save_view['synthetic_resources_only'] or save_view['original_resources_opened'] or save_view['devices_opened'] or save_view['game_launched'] or save_view['screen_access']:raise ValueError('Native save view checks failed')
                run([directory/'wa2_native_preference_tests.exe',directory/'native-preferences.json'],console,90)
                run([directory/'wa2_native_backlog_view_tests.exe',directory/'native-backlog-view.json'],console,90)
                backlog_view=json.loads((directory/'native-backlog-view.json').read_text())
                if not backlog_view['passed'] or backlog_view['cases']<31 or backlog_view['guards']<14 or backlog_view['maximum_pending_images']>1 or backlog_view['maximum_new_images_per_tick']>2 or not backlog_view['four_normal_rows'] or not backlog_view['novel_page_and_scroll'] or not backlog_view['shared_application_bound'] or not backlog_view['voice_replay_keeps_history'] or not backlog_view['toolbar_does_not_advance_story'] or not backlog_view['error_resume_rebases_driver'] or not backlog_view['synthetic_resources_only'] or backlog_view['devices_opened'] or backlog_view['game_launched'] or backlog_view['screen_access']:raise ValueError('Native backlog/application checks failed')
                run([directory/'wa2_native_music_view_tests.exe',directory/'native-music-view.json'],console,90)
                music_view=json.loads((directory/'native-music-view.json').read_text())
                if not music_view['passed'] or music_view['cases']<26 or music_view['guards']<12 or music_view['track_count']!=63 or music_view['maximum_pending_images']>1 or music_view['maximum_new_images_per_tick']>2 or not music_view['source_progress_rules'] or not music_view['two_page_mapping'] or not music_view['title_application_bound'] or not music_view['failed_play_preserves_active_track'] or not music_view['fake_music_only'] or not music_view['synthetic_resources_only'] or music_view['devices_opened'] or music_view['game_launched'] or music_view['screen_access']:raise ValueError('Native music-browser/application checks failed')
                run([directory/'wa2_native_extra_grid_tests.exe',directory/'native-extra-grid.json'],console,90)
                extra_grid=json.loads((directory/'native-extra-grid.json').read_text())
                if not extra_grid['passed'] or extra_grid['cases']<28 or extra_grid['guards']<12 or extra_grid['gallery_groups']!=168 or extra_grid['replay_entries']!=24 or extra_grid['maximum_pending_images']>1 or extra_grid['maximum_new_images_per_tick']>2 or not extra_grid['failed_page_keeps_previous'] or not extra_grid['gallery_variants_only_seen'] or not extra_grid['replay_script_receives_entry_number'] or not extra_grid['replay_entry_save_roundtrip'] or not extra_grid['replay_returns_to_retained_page'] or not extra_grid['shared_application_bound'] or not extra_grid['synthetic_resources_only'] or not extra_grid['fake_audio_only'] or extra_grid['original_resources_opened'] or extra_grid['devices_opened'] or extra_grid['game_launched'] or extra_grid['screen_access']:raise ValueError('Native gallery/replay/application checks failed')
                run([directory/'wa2_native_save_application_tests.exe',directory/'native-save-application.json'],console,90)
                save_application=json.loads((directory/'native-save-application.json').read_text())
                if not save_application['passed'] or save_application['cases']<18 or save_application['guards']<12 or not save_application['common_save_view_bound'] or not save_application['failed_load_preserves_scene'] or not save_application['metadata_and_thumbnail_written'] or not save_application['recoverable_operation_errors'] or not save_application['bounded_preview_reads'] or not save_application['right_click_save_route'] or not save_application['same_mixer_on_return'] or not save_application['synthetic_resources_only'] or save_application['devices_opened'] or save_application['game_launched'] or save_application['screen_access']:raise ValueError('Native save/application checks failed')
                preferences=json.loads((directory/'native-preferences.json').read_text())
                if not preferences['passed'] or preferences['cases']<14 or preferences['guards']<30 or not preferences['memory_storage_only'] or not preferences['fake_voices_only'] or preferences['game_launched'] or preferences['screen_access']:raise ValueError('Native preference checks failed')
                run([directory/'wa2_native_preference_storage_tests.exe',directory/'native-preference-storage.json'],console,90)
                preference_storage=json.loads((directory/'native-preference-storage.json').read_text())
                if not preference_storage['passed'] or preference_storage['cases']<8 or preference_storage['guards']<13 or not preference_storage['synthetic_files_only'] or preference_storage['original_settings_opened'] or preference_storage['game_launched'] or preference_storage['screen_access']:raise ValueError('Native preference file checks failed')
                run([directory/'wa2_native_application_tests.exe',directory/'native-application.json'],console,90)
                application=json.loads((directory/'native-application.json').read_text())
                if not application['passed'] or application['cases']<15 or application['guards']<15 or not application['synthetic_script_only'] or not application['failed_load_preserves_scene'] or not application['menu_keeps_audio'] or not application['background_freezes_audio'] or application['original_resources_opened'] or application['devices_opened'] or application['game_launched'] or application['screen_access']:raise ValueError('Native application state checks failed')
                run([directory/'wa2_native_title_view_tests.exe',directory/'native-title-view.json'],console,90)
                title_view=json.loads((directory/'native-title-view.json').read_text())
                if not title_view['passed'] or title_view['cases']<21 or title_view['guards']<6 or not title_view['synthetic_images_only'] or title_view['original_resources_opened'] or title_view['devices_opened'] or title_view['game_launched'] or title_view['screen_access']:raise ValueError('Native title view checks failed')
                run([directory/'wa2_native_options_view_tests.exe',directory/'native-options-view.json'],console,90)
                options_view=json.loads((directory/'native-options-view.json').read_text())
                if not options_view['passed'] or options_view['cases']<24 or options_view['guards']<60 or options_view['maximum_pending_images']>1 or options_view['maximum_new_requests_per_tick']>2 or not options_view['synthetic_images_only'] or options_view['preference_storage_bound'] or options_view['application_view_bound'] or options_view['original_resources_opened'] or options_view['devices_opened'] or options_view['game_launched'] or options_view['screen_access']:raise ValueError('Native options view checks failed')
                run([directory/'wa2_native_options_preference_tests.exe',directory/'native-options-preferences.json'],console,90)
                options_preferences=json.loads((directory/'native-options-preferences.json').read_text())
                if not options_preferences['passed'] or options_preferences['cases']<34 or options_preferences['guards']<15 or not options_preferences['legacy_preferences_preserved'] or not options_preferences['all_visible_cells_mapped'] or not options_preferences['opacity_256_saturates'] or not options_preferences['mute_keeps_live_voice'] or options_preferences['source_text_budget_steps']!=[0,4,2,1] or not options_preferences['synthetic_resources_only'] or not options_preferences['fake_voices_only'] or options_preferences['original_resources_opened'] or options_preferences['devices_opened'] or options_preferences['game_launched'] or options_preferences['screen_access']:raise ValueError('Native options preferences checks failed')
                run([directory/'wa2_native_options_application_tests.exe',directory/'native-options-application.json'],console,90)
                options_application=json.loads((directory/'native-options-application.json').read_text())
                if not options_application['passed'] or options_application['cases']<19 or options_application['guards']<9 or not options_application['source_backends_shared'] or not options_application['display_write_failure_rolls_back'] or not options_application['modal_story_frozen'] or not options_application['same_mixer_on_return'] or not options_application['preferences_persisted_before_apply'] or not options_application['held_slider_input_bound'] or not options_application['unsupported_actions_explicit'] or not options_application['synthetic_resources_only'] or options_application['original_resources_opened'] or options_application['devices_opened'] or options_application['game_launched'] or options_application['screen_access']:raise ValueError('Native options/application checks failed')
                run([directory/'wa2_native_options_action_tests.exe',directory/'native-options-actions.json'],console,90)
                options_actions=json.loads((directory/'native-options-actions.json').read_text())
                if not options_actions['passed'] or options_actions['cases']<28 or options_actions['guards']<16 or not options_actions['source_modal_geometry'] or not options_actions['confirmation_gates_reset'] or not options_actions['source_defaults_preserved'] or not options_actions['previews_replace_bounded_channels'] or not options_actions['no_preview_story_history'] or not options_actions['same_mixer_from_title_and_story'] or not options_actions['right_click_settings_route'] or not options_actions['synthetic_resources_only'] or options_actions['original_resources_opened'] or options_actions['devices_opened'] or options_actions['game_launched'] or options_actions['screen_access']:raise ValueError('Native options action checks failed')
                run([directory/'wa2_native_title_scene_tests.exe',directory/'native-title-scene.json'],console,90)
                title_scene=json.loads((directory/'native-title-scene.json').read_text())
                if not title_scene['passed'] or title_scene['cases']<20 or title_scene['guards']<13 or title_scene['audio_lookup_cases']<12 or not title_scene['synthetic_resources_only'] or not title_scene['fake_music_and_movies'] or not title_scene['title_story_handoff'] or title_scene['devices_opened'] or title_scene['game_launched'] or title_scene['screen_access']:raise ValueError('Native title/application integration checks failed')
                run([directory/'wa2_native_render_frame_tests.exe',directory/'native-render-frame.json'],console,90)
                render_frame=json.loads((directory/'native-render-frame.json').read_text())
                if not render_frame['passed'] or render_frame['cases']<23 or render_frame['guards']<3 or not render_frame['source_layer_order'] or not render_frame['equal_keys_stable'] or not render_frame['owned_snapshot'] or not render_frame['capture_does_not_advance_logic'] or not render_frame['synthetic_resources_only'] or render_frame['original_resources_opened'] or render_frame['devices_opened'] or render_frame['game_launched'] or render_frame['screen_access']:raise ValueError('Native frame assembly checks failed')
                run([directory/'wa2_native_movie_state_tests.exe',directory/'native-movie-state.json'],console,90)
                movie_state=json.loads((directory/'native-movie-state.json').read_text())
                if not movie_state['passed'] or movie_state['cases']<28 or movie_state['guards']<12 or not movie_state['source_preplay_audio_order'] or not movie_state['movie_gain_follows_preferences'] or not movie_state['visual_state_preserved'] or not movie_state['services_survive_save_load'] or not movie_state['fake_transports_only'] or not movie_state['synthetic_resources_only'] or movie_state['original_resources_opened'] or movie_state['devices_opened'] or movie_state['game_launched'] or movie_state['screen_access']:raise ValueError('Native movie integration checks failed')
                run([directory/'wa2_native_frame_driver_tests.exe',directory/'native-frame-driver.json'],console,90)
                frame_driver=json.loads((directory/'native-frame-driver.json').read_text())
                if not frame_driver['passed'] or frame_driver['cases']<27 or frame_driver['guards']<16 or frame_driver['logic_hz']!=60 or not frame_driver['bounded_catch_up'] or not frame_driver['shared_skip_budget'] or not frame_driver['queued_click_keeps_position'] or not frame_driver['synthetic_clock_and_scripts_only'] or frame_driver['performance_benchmark'] or frame_driver['original_resources_opened'] or frame_driver['devices_opened'] or frame_driver['game_launched'] or frame_driver['screen_access']:raise ValueError('Native frame driver checks failed')
                run([directory/'wa2_native_audio_binding_tests.exe',directory/'native-audio-binding.json'],console,90)
                audio_binding=json.loads((directory/'native-audio-binding.json').read_text())
                if not audio_binding['passed'] or audio_binding['cases']<14 or audio_binding['guards']<5 or audio_binding['peak_live_outputs']!=1 or not audio_binding['mixer_alive_until_detach'] or not audio_binding['unchanged_owner_keeps_output'] or not audio_binding['fake_outputs_only'] or audio_binding['devices_opened'] or audio_binding['game_launched'] or audio_binding['screen_access']:raise ValueError('Native audio binding checks failed')
                run([directory/'wa2_native_image_wait_tests.exe',directory/'native-image-wait.json'],console,90)
                image_wait=json.loads((directory/'native-image-wait.json').read_text())
                if not image_wait['passed'] or image_wait['cases']<13 or image_wait['guards']<7 or not image_wait['completion_notification'] or not image_wait['multiple_waiters_same_owner'] or not image_wait['pending_shutdown_notifies'] or not image_wait['synthetic_resources_only'] or image_wait['original_resources_opened'] or image_wait['devices_opened'] or image_wait['game_launched'] or image_wait['screen_access']:raise ValueError('Native image completion wait checks failed')
                run([directory/'wa2_native_bootstrap_tests.exe',directory/'native-bootstrap.json',directory/'native-bootstrap-owned'],console,90)
                bootstrap=json.loads((directory/'native-bootstrap.json').read_text())
                if not bootstrap['passed'] or bootstrap['cases']<17 or bootstrap['guards']<11 or not bootstrap['source_ic_presence_rule'] or not bootstrap['title_story_storage_connected'] or not bootstrap['native_preferences_reopened'] or not bootstrap['saved_image_wait_not_polled'] or not bootstrap['test_mode_persisted_after_prepare'] or not bootstrap['failed_prepare_writes_nothing'] or not bootstrap['fake_movies_only'] or not bootstrap['synthetic_resources_only'] or bootstrap['original_resources_opened'] or bootstrap['devices_opened'] or bootstrap['game_launched'] or bootstrap['screen_access']:raise ValueError('Native shared bootstrap checks failed')
                run([directory/'wa2_android_jni_service_tests.exe',directory/'android-jni-services.json'],console,90)
                run([directory/'wa2_native_desktop_tests.exe',directory/'native-desktop.json',directory/'native-desktop-owned'],console,90)
                desktop=json.loads((directory/'native-desktop.json').read_text())
                if not desktop['passed'] or desktop['cases']<13 or desktop['guards']<20 or not all(desktop[key] for key in ('external_resource_discovery','unicode_directories','provider_failures_not_missing','metadata_only_discovery','synthetic_files_only')) or any(desktop[key] for key in ('original_resources_opened','devices_opened','game_launched','screen_access')):raise ValueError('Native desktop resource/argument checks failed')
                jni_services=json.loads((directory/'android-jni-services.json').read_text())
                if not jni_services['passed'] or jni_services['cases']<21 or jni_services['guards']<6 or not all(jni_services[key] for key in
                        ('native_and_retained_service_sets','partial_registration_unwound','reverse_release_order','retry_after_failure','fake_services_only')) or any(jni_services[key] for key in
                        ('java_vm_created','devices_opened','game_launched','screen_access')):raise ValueError('Android JNI service lifecycle checks failed')
                shader_directory=directory/'mask-shaders'
                run([directory/'wa2_native_mask_source_tests.exe',shader_directory],console,90)
                for profile in ('desktop','embedded'):
                    vertex=shader_directory/('native-mask-'+profile+'.vert');fragment=shader_directory/('native-mask-'+profile+'.frag')
                    run([args.shader_compiler.resolve(),'-l',vertex,fragment],console,90)
                    mask_shader_checks.append(dict(profile=profile,vertex_sha256=digest(vertex),fragment_sha256=digest(fragment),offline_compile_and_link=True))
                if args.sdl_archive:
                    run([directory/'wa2_native_scene_composition_tests.exe',directory/'native-scene-composition.json'],console,90)
                    composition_state=json.loads((directory/'native-scene-composition.json').read_text())
                    if not composition_state['passed'] or composition_state['cases']<45 or not composition_state['owned_memory_only'] or composition_state['windows_created'] or composition_state['devices_opened'] or composition_state['game_launched'] or composition_state['screen_access']:raise ValueError('Native owned-memory composition checks failed')
                    composition_graph=output([ninja,'-C',directory,'-t','graph','wa2_native_scene_composition_tests.exe']).replace('\\','/').lower()
                    if any(token in composition_graph for token in forbidden):raise ValueError('AOT dependency in owned-memory composition test')
                    composition_artifact=dict(sha256=digest(directory/'wa2_native_scene_composition_tests.exe'),
                        native_link_graph_sha256=hashlib.sha256(composition_graph.encode()).hexdigest())
                    run([directory/'wa2_native_sdl_application_tests.exe',directory/'native-sdl-application.json'],console,90)
                    sdl_application=json.loads((directory/'native-sdl-application.json').read_text())
                    if not sdl_application['passed'] or sdl_application['cases']<17 or sdl_application['guards']<2 or not sdl_application['owned_memory_only'] or not sdl_application['memory_renderer_has_no_window_services'] or not sdl_application['fake_audio_outputs_only'] or not sdl_application['actual_present_hook'] or not sdl_application['synthetic_event_structs_only'] or sdl_application['os_event_queue_polled'] or sdl_application['original_resources_opened'] or sdl_application['windows_created'] or sdl_application['devices_opened'] or sdl_application['game_launched'] or sdl_application['screen_access']:raise ValueError('Native SDL application pump checks failed')
                    sdl_graph=output([ninja,'-C',directory,'-t','graph','wa2_native_sdl_application_tests.exe']).replace('\\','/').lower()
                    if any(token in sdl_graph for token in forbidden):raise ValueError('AOT dependency in owned-memory application pump test')
                    run([directory/'wa2_native_input_tests.exe',directory/'native-input.json'],console,90)
                    input_state=json.loads((directory/'native-input.json').read_text())
                    if not input_state['passed'] or input_state['cases']<14 or not input_state['synthetic_event_structs_only'] or input_state['os_event_queue_polled'] or input_state['os_input_injected'] or input_state['windows_created'] or input_state['screen_access'] or input_state['game_launched']:raise ValueError('Native synthetic input checks failed')
                if args.resource_root:
                    run([directory/'wa2_native_script_catalogue_tests.exe',args.resource_root.resolve(),directory/'native-script-catalogue.json'],console,90)
                    catalogue=json.loads((directory/'native-script-catalogue.json').read_text())
                    if not catalogue['passed'] or catalogue['calendar_atlas_headers']!=4 or catalogue['selection_atlas_headers']!=2 or catalogue['colour_maps_parsed']!=2 or not catalogue['sprite_animations'] or not catalogue['animation_frames'] or catalogue['animation_sequences_executed'] or catalogue['script_executed'] or catalogue['game_launched']:raise ValueError('Invalid static resource script audit')
    if sources['windows']!=sources['android']:raise ValueError('Native game sources differ between host platforms')
    for name,sha in bindings.items():
        if digest(ROOT/name)!=sha:raise ValueError('Native verification source changed: '+name)
    if args.sdl_archive and digest(args.sdl_archive)!=composition_sdl_sha:raise ValueError('SDL archive changed during owned-memory verification')
    if args.android_sdl_library and digest(args.android_sdl_library)!=android_sdl_sha:raise ValueError('Android SDL2 library changed during entry link verification')
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
    report['background_state']=background_state
    report['character_state']=character_state
    report['bitmap_state']=bitmap_state
    report['weather_checkpoint']=weather_checkpoint
    report['audio_checkpoint']=audio_checkpoint
    report['saved_scene']=saved_scene
    report['scene_checkpoint']=scene_checkpoint
    report['save_slots']=save_slots
    report['save_document']=save_document
    report['save_view']=save_view
    report['save_application']=save_application
    report['backlog_view']=backlog_view
    report['music_view']=music_view
    report['extra_grid']=extra_grid
    report['preferences']=preferences
    report['preference_storage']=preference_storage
    report['application']=application
    report['title_view']=title_view
    report['options_view']=options_view
    report['options_preferences']=options_preferences
    report['options_application']=options_application
    report['options_actions']=options_actions
    report['title_scene']=title_scene
    report['render_frame']=render_frame
    report['movie_state']=movie_state
    report['frame_driver']=frame_driver
    report['audio_binding']=audio_binding
    report['sdl_application']=sdl_application
    report['input_state']=input_state
    report['image_wait']=image_wait
    report['bootstrap']=bootstrap
    report['desktop']=desktop
    report['windows_native_entry']=windows_entry_artifact
    report['android_native_host_compiled']=True
    report['android_native_host_linked']=android_entry_artifact is not None
    report['android_native_entry']=android_entry_artifact
    report['android_sdl_library_sha256']=android_sdl_sha
    report['android_jni_services']=jni_services
    report['retained_android_jni_compiled']=True
    (ROOT/'logs/native-core-verification.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PASS shared native core: '+str(len(sources['windows']))+' identical common sources, full link on x64/ARM64, no AOT dependency. This is not a complete game.',flush=True)


if __name__=='__main__':main()
