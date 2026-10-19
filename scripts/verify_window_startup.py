"""Bounded memory-only window, pointer-coordinate and cached API-prefix checks."""
import ctypes
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[1]


def main():
    kernel=ctypes.WinDLL('kernel32',use_last_error=True);kernel.GetCurrentProcess.restype=ctypes.c_void_p
    kernel.SetPriorityClass.argtypes=[ctypes.c_void_p,ctypes.c_uint]
    if not kernel.SetPriorityClass(kernel.GetCurrentProcess(),0x4000):raise ctypes.WinError(ctypes.get_last_error())
    flags=subprocess.BELOW_NORMAL_PRIORITY_CLASS|subprocess.CREATE_NO_WINDOW
    env=os.environ.copy();env.update(CMAKE_BUILD_PARALLEL_LEVEL='1',TEMP=str(ROOT/'build/temp'),TMP=str(ROOT/'build/temp'),
        ZIG_GLOBAL_CACHE_DIR=str(ROOT/'build/zig-cache'),ZIG_LOCAL_CACHE_DIR=str(ROOT/'build/zig-local'))
    files=[p for folder in ('recompiled','engine','generated/shared-aot','generated/static-data','platform/window')
           for p in (ROOT/folder).glob('*') if p.is_file()]
    files += [Path(__file__),ROOT/'tests/window_model.cpp']
    hashes={p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
    build=ROOT/'build/shared-aot-x64';cmake=ROOT/'tools/python/cmake/data/bin/cmake.exe'
    with (ROOT/'logs/window-startup-build.log').open('wb') as log:
        def run(command,timeout=180):
            with subprocess.Popen(list(map(str,command)),cwd=ROOT,env=env,creationflags=flags,stdout=log,stderr=subprocess.STDOUT) as child:
                try:code=child.wait(timeout)
                except subprocess.TimeoutExpired:
                    subprocess.run(['taskkill','/PID',str(child.pid),'/T','/F'],creationflags=flags,stdout=log,stderr=subprocess.STDOUT,timeout=20);child.wait(20);raise
                if code:raise subprocess.CalledProcessError(code,command)
        run([cmake,'-S',ROOT/'recompiled','-B',build,'-G','Ninja','-DCMAKE_TOOLCHAIN_FILE='+str(ROOT/'scripts/zig-toolchain.cmake'),
            '-DCMAKE_BUILD_TYPE=MinSizeRel','-DWA2_SHARED_AOT_CHECKS=ON','-DWA2_AOT_DIAGNOSTICS=ON','-DCMAKE_EXPORT_COMPILE_COMMANDS=ON'])
        run([cmake,'--build',build,'--target','wa2_window_model_tests','--parallel','1'],900)
        run([build/'wa2_window_model_tests.exe',build/'window-model.json'],30)
    for name,sha in hashes.items():
        if hashlib.sha256((ROOT/name).read_bytes()).hexdigest()!=sha:raise ValueError('Window input changed during checks: '+name)
    proof=dict(schema='wa2.window-startup-verification.v1',source_sha256=hashes,
        checks=json.loads((build/'window-model.json').read_text()),game_launched=False,screen_access=False,
        graphics_device_created=False,source_executable_opened=False,passed=True)
    (ROOT/'logs/window-startup-verification.json').write_text(json.dumps(proof,indent=2)+'\n')
    print('PASS window/pointer ABI, DPI/letterbox mappings and cached API prefixes, using memory only.')


if __name__=='__main__':main()
