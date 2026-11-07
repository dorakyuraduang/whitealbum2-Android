"""Configure the full native Windows/SDL build without building or launching it.

The existing native-core report must match the current sources. This additional
check audits the real production CMake root/source closure, not a game session.
"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[1]
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--zig-cache',type=Path,default=ROOT/'build/zig-cache')
    args=parser.parse_args()
    if os.name!='nt':raise SystemExit('This root check uses the existing Windows x64 codec/toolchain')
    kernel=ctypes.WinDLL('kernel32',use_last_error=True);kernel.GetCurrentProcess.restype=ctypes.c_void_p
    kernel.SetPriorityClass.argtypes=[ctypes.c_void_p,ctypes.c_uint]
    kernel.GetProcessAffinityMask.argtypes=[ctypes.c_void_p,ctypes.POINTER(ctypes.c_size_t),ctypes.POINTER(ctypes.c_size_t)]
    kernel.SetProcessAffinityMask.argtypes=[ctypes.c_void_p,ctypes.c_size_t]
    process=kernel.GetCurrentProcess();current=ctypes.c_size_t();system=ctypes.c_size_t()
    if not kernel.SetPriorityClass(process,0x4000) or not kernel.GetProcessAffinityMask(process,ctypes.byref(current),ctypes.byref(system)) or not kernel.SetProcessAffinityMask(process,current.value&-current.value):raise ctypes.WinError()
    source_report=ROOT/'logs/native-core-verification.json';core=json.loads(source_report.read_text())
    if not core['passed'] or not core.get('windows_native_entry',{}).get('actual_entry_symbols_audited'):raise ValueError('A verified native Windows entry is required first')
    inputs=dict(core['source_sha256'])
    for path in [Path(__file__),*sorted((ROOT/'tools/SDL-release-2.32.10/cmake').rglob('*.cmake')),ROOT/'tools/SDL-release-2.32.10/CMakeLists.txt']:
        name=path.relative_to(ROOT).as_posix();inputs[name]=digest(path)
    for name,value in core['source_sha256'].items():
        if digest(ROOT/name)!=value:raise ValueError('Native core input changed: '+name)
    for name,value in core['codec_archives_sha256'].items():
        if digest(ROOT/name)!=value:raise ValueError('Native codec input changed: '+name)
    directory=ROOT/'build/native-desktop-definition';directory.mkdir(parents=True,exist_ok=True)
    env=os.environ.copy();env['CMAKE_BUILD_PARALLEL_LEVEL']='1';env['ZIG_GLOBAL_CACHE_DIR']=str(args.zig_cache.resolve());env['ZIG_LOCAL_CACHE_DIR']=str(directory/'zig-local')
    args.zig_cache.mkdir(parents=True,exist_ok=True);(directory/'zig-local').mkdir(exist_ok=True)
    for name in ('TEMP','TMP'):env[name]=str(directory/'temp')
    (directory/'temp').mkdir(exist_ok=True)
    cmake=ROOT/'tools/python/cmake/data/bin/cmake.exe';ninja=ROOT/'tools/python/bin/ninja.exe';flags=subprocess.CREATE_NO_WINDOW|subprocess.BELOW_NORMAL_PRIORITY_CLASS
    command=[cmake,'-S',ROOT/'hosts/native','-B',directory,'-G','Ninja','-DCMAKE_TOOLCHAIN_FILE='+str(ROOT/'scripts/zig-toolchain.cmake'),
        '-DCMAKE_MAKE_PROGRAM='+str(ninja),'-DCMAKE_BUILD_TYPE=MinSizeRel','-DCMAKE_EXPORT_COMPILE_COMMANDS=ON']
    print('Configuring the full native desktop SDL root; one hidden low-priority job. No game/device starts.',flush=True)
    with (ROOT/'logs/native-desktop-definition.log').open('wb') as log:
        subprocess.run(list(map(str,command)),cwd=ROOT,env=env,creationflags=flags,stdout=log,stderr=subprocess.STDOUT,timeout=600,check=True)
    graph=subprocess.run([str(ninja),'-C',str(directory),'-t','graph','WA2_Native_x64.exe'],cwd=ROOT,env=env,creationflags=flags,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=60,check=True).stdout.replace('\\','/').lower()
    for token in ('recompiled/','generated/','winrecomp','wa2_function_process','wa2_shared_aot','/port/'):
        if token in graph:raise ValueError('Non-native desktop dependency: '+token)
    commands=json.loads((directory/'compile_commands.json').read_text());sources=sorted({Path(item['file']).resolve().relative_to(ROOT).as_posix() for item in commands if Path(item['output']).resolve().relative_to(directory).as_posix().lower() in graph})
    expected=set(core['native_compiled_sources'])
    if not expected.issubset(sources):raise ValueError('Full SDL desktop root omits common game sources: '+str(sorted(expected-set(sources))))
    for name in ('platform/native_host.cpp','platform/native_desktop.cpp','hosts/native/windows_main.cpp','tools/SDL-release-2.32.10/src/main/windows/SDL_windows_main.c',
            'tools/SDL-release-2.32.10/src/video/windows/SDL_windowsvideo.c','tools/SDL-release-2.32.10/src/render/opengl/SDL_render_gl.c'):
        if name not in sources:raise ValueError('Missing actual desktop SDL/device source: '+name)
    if not any('/src/audio/wasapi/' in path or '/src/audio/winmm/' in path for path in sources):raise ValueError('Native desktop root omits a production audio backend')
    cache=(directory/'CMakeCache.txt').read_text(encoding='utf-8')
    # SDL2 selects the Windows video backend from the target platform; there is
    # no SDL_WINDOWS cache option. Its source closure is checked above.
    if any(name+':BOOL=ON' not in cache for name in ('SDL_OPENGL','SDL_VIDEO','SDL_AUDIO','SDL_RENDER')):raise ValueError('Native desktop root lacks its production video/render/audio subsystems')
    for name,value in inputs.items():
        if digest(ROOT/name)!=value:raise ValueError('Desktop definition changed during its check: '+name)
    report=dict(schema='wa2.native-desktop-definition.v1',source_sha256=inputs,native_core_report_sha256=digest(source_report),
        compile_commands_sha256=digest(directory/'compile_commands.json'),link_graph_sha256=hashlib.sha256(graph.encode()).hexdigest(),
        common_game_sources=len(expected),compiled_source_definitions=sources,production_sdl_window_gl_audio_backends=True,
        configured_only=True,game_built=False,executable_started=False,devices_opened=False,screen_access=False,passed=True)
    (ROOT/'logs/native-desktop-definition.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print('PASS full native desktop build definition: shared game and actual SDL window/OpenGL/audio sources; no game launched.')

if __name__=='__main__':main()
