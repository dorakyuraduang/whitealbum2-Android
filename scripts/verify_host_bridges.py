"""Bounded host-bridge checks, without rerunning the unchanged numerical suite."""
import ctypes
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[1]
def main():
    kernel=ctypes.WinDLL('kernel32');kernel.GetCurrentProcess.restype=ctypes.c_void_p;kernel.SetPriorityClass.argtypes=[ctypes.c_void_p,ctypes.c_uint]
    if not kernel.SetPriorityClass(kernel.GetCurrentProcess(),0x4000):raise ctypes.WinError(ctypes.get_last_error())
    env=os.environ.copy();env.update(TEMP=str(ROOT/'build/temp'),TMP=str(ROOT/'build/temp'),ZIG_GLOBAL_CACHE_DIR=str(ROOT/'build/zig-cache'),ZIG_LOCAL_CACHE_DIR=str(ROOT/'build/zig-local'),CMAKE_BUILD_PARALLEL_LEVEL='1')
    flags=subprocess.BELOW_NORMAL_PRIORITY_CLASS|subprocess.CREATE_NO_WINDOW
    tracked=[p for d in ('platform/window','platform/render','platform/compat','android/native','android/app/src/main/java','recompiled') for p in (ROOT/d).rglob('*') if p.is_file()]
    tracked += [ROOT/'tests/window_model.cpp',Path(__file__)]
    tracked += [ROOT/'tests/portable_movie_bridge.cpp',ROOT/'port/audio.cpp',ROOT/'port/audio.hpp',ROOT/'tools/WindowsRecompTool-main/src/runtime/process.cpp']
    tracked += [ROOT/('engine/'+name) for name in ('movie.cmake','wave_codec.cpp','wave_codec.hpp')]
    tracked += [ROOT/('tests/'+name) for name in ('portable_acm.cpp','portable_shell.cpp')]
    tracked += [ROOT/'tools/WindowsRecompTool-main/src/runtime/win32.cpp',ROOT/'tools/WindowsRecompTool-main/include/winrecomp/gui.hpp']
    tracked += [p for p in (ROOT/'hosts').rglob('*') if p.is_file()]
    tracked += [ROOT/'tests/shared-aot-android/CMakeLists.txt']
    hashes={p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in tracked}
    x64=ROOT/'build/shared-aot-x64';arm=ROOT/'build/shared-aot-android';cmake=ROOT/'tools/python/cmake/data/bin/cmake.exe';acmake=ROOT/'tools/android-sdk/cmake/3.22.1/bin/cmake.exe';ndk=ROOT/'tools/android-sdk/ndk/27.0.12077973'
    with (ROOT/'logs/host-bridge-build.log').open('wb') as console:
        def run(command,timeout=600):
            with subprocess.Popen(list(map(str,command)),cwd=ROOT,env=env,creationflags=flags,stdout=console,stderr=subprocess.STDOUT) as child:
                try:status=child.wait(timeout)
                except subprocess.TimeoutExpired:
                    subprocess.run(['taskkill','/PID',str(child.pid),'/T','/F'],creationflags=flags,stdout=console,stderr=subprocess.STDOUT,timeout=20);child.wait(20);raise
                if status:raise subprocess.CalledProcessError(status,command)
        run([cmake,'-S',ROOT/'recompiled','-B',x64,'-G','Ninja','-DCMAKE_TOOLCHAIN_FILE='+str(ROOT/'scripts/zig-toolchain.cmake'),'-DCMAKE_BUILD_TYPE=MinSizeRel','-DWA2_SHARED_AOT_CHECKS=ON','-DWA2_AOT_DIAGNOSTICS=ON','-DCMAKE_EXPORT_COMPILE_COMMANDS=ON'])
        run([cmake,'--build',x64,'--target','wa2_window_model_tests','wa2_portable_movie_bridge_tests','wa2_portable_sound_bridge_tests','wa2_portable_scheduler_tests','wa2_portable_acm_tests','wa2_portable_shell_tests','--parallel','1'])
        run([x64/'wa2_window_model_tests.exe',x64/'window-model.json'],30)
        run([x64/'wa2_portable_movie_bridge_tests.exe',x64/'movie-bridge.json'],30)
        run([x64/'wa2_portable_sound_bridge_tests.exe',x64/'sound-bridge.json'],30)
        run([x64/'wa2_portable_scheduler_tests.exe',x64/'portable-scheduler.json'],30)
        run([x64/'wa2_portable_acm_tests.exe',x64/'portable-acm.json'],30)
        run([x64/'wa2_portable_shell_tests.exe',x64/'portable-shell.json'],30)
        run([sys.executable,ROOT/'scripts/verify_android_java.py'],90)
        run([acmake,'-S',ROOT/'tests/shared-aot-android','-B',arm,'-G','Ninja','-DCMAKE_MAKE_PROGRAM='+str(ROOT/'tools/android-sdk/cmake/3.22.1/bin/ninja.exe'),'-DCMAKE_TOOLCHAIN_FILE='+str(ndk/'build/cmake/android.toolchain.cmake'),'-DANDROID_ABI=arm64-v8a','-DANDROID_PLATFORM=android-26','-DANDROID_STL=c++_shared','-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON','-DCMAKE_BUILD_TYPE=MinSizeRel','-DWA2_AOT_DIAGNOSTICS=OFF','-DCMAKE_EXPORT_COMPILE_COMMANDS=ON'])
        run([acmake,'--build',arm,'--target','wa2_shared_aot_link_check','wa2_android','--parallel','1'])
    for name,digest in hashes.items():
        if hashlib.sha256((ROOT/name).read_bytes()).hexdigest()!=digest:raise ValueError('host-bridge source changed during checks: '+name)
    proof=dict(schema='wa2.host-bridge-verification.v1',source_sha256=hashes,window_model=json.loads((x64/'window-model.json').read_text()),movie_bridge=json.loads((x64/'movie-bridge.json').read_text()),sound_bridge=json.loads((x64/'sound-bridge.json').read_text()),scheduler=json.loads((x64/'portable-scheduler.json').read_text()),acm=json.loads((x64/'portable-acm.json').read_text()),shell=json.loads((x64/'portable-shell.json').read_text()),arm64_link_verified=True,numerical_suite_rerun=False,graphics_device_created=False,game_launched=False,screen_access=False,whole_game_verified=False,passed=True)
    (ROOT/'logs/host-bridge-verification.json').write_text(json.dumps(proof,indent=2)+'\n');print('PASS common window/effect/display bridges, native widget compilation and ARM64 link; no device or whole-game claim.')
if __name__=='__main__':main()
