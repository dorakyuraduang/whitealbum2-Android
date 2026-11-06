"""Build the shared native migration core and run synthetic progress checks.

No game, emulator, screen access, original EXE or real resource/save is used.
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
        ROOT/'tests/native_progress.cpp',ROOT/'tests/native_core_link.cpp',ROOT/'platform/progress_storage.cpp',ROOT/'platform/progress_storage.hpp',
        ROOT/'scripts/zig-toolchain.cmake']
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
    sources={};artifacts={};codec_bindings={}
    with (ROOT/'logs/native-core-build.log').open('wb') as console:
        for host in ('windows','android'):
            directory=build/host
            configure=[cmake,'-S',ROOT/'native','-B',directory,'-G','Ninja','-DCMAKE_BUILD_TYPE=MinSizeRel',
                '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON','-DCMAKE_MAKE_PROGRAM='+str(ninja)]
            if host=='windows':configure+=['-DCMAKE_TOOLCHAIN_FILE='+str(ROOT/'scripts/zig-toolchain.cmake')]
            else:configure+=['-DCMAKE_TOOLCHAIN_FILE='+str(ndk/'build/cmake/android.toolchain.cmake'),'-DANDROID_ABI=arm64-v8a','-DANDROID_PLATFORM=android-26','-DANDROID_STL=c++_shared']
            print('Configuring and building '+host+' native core; one hidden low-priority job.',flush=True)
            run(configure,console)
            targets=['wa2_native_link_check']+(['wa2_native_progress_tests'] if host=='windows' else [])
            run([cmake,'--build',directory,'--target',*targets,'--parallel','1'],console,2400)
            commands=json.loads((directory/'compile_commands.json').read_text())
            names={Path(item['file']).resolve().relative_to(ROOT).as_posix() for item in commands}
            allowed=('engine/','game/','platform/progress_storage.cpp','tests/native_core_link.cpp','tests/native_progress.cpp','tests/pcm_audio.cpp')
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
    if sources['windows']!=sources['android']:raise ValueError('Native game sources differ between host platforms')
    for name,sha in bindings.items():
        if digest(ROOT/name)!=sha:raise ValueError('Native verification source changed: '+name)
    report=dict(schema='wa2.native-core.v1',source_sha256=bindings,codec_archives_sha256=codec_bindings,
        native_compiled_sources=sources['windows'],same_native_game_sources=True,artifacts=artifacts,progress=checks,
        recompiled_game_linked=False,guest_cpu_and_memory_linked=False,source_executable_opened=False,screen_access=False,
        game_launched=False,emulator_launched=False,android_device_verified=False,complete_gameplay_verified=False,
        native_engine_is_complete=False,apk_created=False,current_release_replaced=False,maximum_build_jobs=1,passed=True)
    (ROOT/'logs/native-core-verification.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PASS shared native core: '+str(len(sources['windows']))+' identical common sources, full link on x64/ARM64, no AOT dependency. This is not a complete game.',flush=True)


if __name__=='__main__':main()
