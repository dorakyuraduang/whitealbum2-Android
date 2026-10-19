"""Small, bounded CPU leaf snapshots only. Never starts a game or reads pixels."""
import ctypes
import os
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]

def main():
    kernel=ctypes.WinDLL('kernel32');kernel.GetCurrentProcess.restype=ctypes.c_void_p
    kernel.SetPriorityClass.argtypes=[ctypes.c_void_p,ctypes.c_uint]
    if not kernel.SetPriorityClass(kernel.GetCurrentProcess(),0x4000):raise ctypes.WinError()
    environment=os.environ.copy();environment.update(CMAKE_BUILD_PARALLEL_LEVEL='1',TEMP=str(ROOT/'build/temp'),TMP=str(ROOT/'build/temp'),ZIG_GLOBAL_CACHE_DIR=str(ROOT/'build/zig-cache'),ZIG_LOCAL_CACHE_DIR=str(ROOT/'build/zig-local'))
    flags=subprocess.CREATE_NO_WINDOW|subprocess.BELOW_NORMAL_PRIORITY_CLASS
    cmake=ROOT/'tools/python/cmake/data/bin/cmake.exe';build=ROOT/'build/shared-aot-x64'
    with (ROOT/'logs/render-cpu-build.log').open('wb') as console:
        for command in ([cmake,'-S',ROOT/'recompiled','-B',build,'-G','Ninja','-DCMAKE_TOOLCHAIN_FILE='+str(ROOT/'scripts/zig-toolchain.cmake'),'-DCMAKE_BUILD_TYPE=MinSizeRel','-DWA2_SHARED_AOT_CHECKS=ON','-DWA2_AOT_DIAGNOSTICS=ON'],
                        [cmake,'--build',build,'--target','wa2_render_cpu_tests','--parallel','1'],
                        [build/'wa2_render_cpu_tests.exe',ROOT/('logs/render-cpu-24bit.json' if '--pc24' in sys.argv else 'logs/render-cpu-component.json'),*(['--pc24'] if '--pc24' in sys.argv else [])]):
            with subprocess.Popen([str(x) for x in command],cwd=ROOT,env=environment,creationflags=flags,stdout=console,stderr=subprocess.STDOUT) as child:
                try:status=child.wait(2400 if '--build' in command else 30)
                except subprocess.TimeoutExpired:
                    subprocess.run(['taskkill','/PID',str(child.pid),'/T','/F'],creationflags=flags,stdout=console,stderr=subprocess.STDOUT,timeout=20)
                    child.wait(20);raise
                if status:raise subprocess.CalledProcessError(status,command)
    print('PASS bounded CPU snapshots, no game launch.')

if __name__=='__main__':main()
