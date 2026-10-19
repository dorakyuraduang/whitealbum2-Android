"""Memory-only floating-point checks; single low-priority build, no game/EXE."""
import ctypes
import os
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]


def main():
    kernel=ctypes.WinDLL('kernel32',use_last_error=True);kernel.GetCurrentProcess.restype=ctypes.c_void_p
    kernel.SetPriorityClass.argtypes=[ctypes.c_void_p,ctypes.c_uint]
    if not kernel.SetPriorityClass(kernel.GetCurrentProcess(),0x4000):raise ctypes.WinError(ctypes.get_last_error())
    environment=os.environ.copy();environment.update(CMAKE_BUILD_PARALLEL_LEVEL='1',TEMP=str(ROOT/'build/temp'),TMP=str(ROOT/'build/temp'),ZIG_GLOBAL_CACHE_DIR=str(ROOT/'build/zig-cache'),ZIG_LOCAL_CACHE_DIR=str(ROOT/'build/zig-local'))
    flags=subprocess.BELOW_NORMAL_PRIORITY_CLASS|subprocess.CREATE_NO_WINDOW
    cmake=ROOT/'tools/python/cmake/data/bin/cmake.exe';build=ROOT/'build/fp-kernel-x64'
    with (ROOT/'logs/fp-kernel-build.log').open('wb') as console:
        def run(command,timeout=300):
            subprocess.run([str(x) for x in command],cwd=ROOT,env=environment,creationflags=flags,stdout=console,stderr=subprocess.STDOUT,check=True,timeout=timeout)
        run([sys.executable,ROOT/'scripts/generate_fp_kernel_cases.py'])
        run([cmake,'-S',ROOT/'tests/fp-kernel','-B',build,'-G','Ninja','-DCMAKE_TOOLCHAIN_FILE='+str(ROOT/'scripts/zig-toolchain.cmake'),'-DCMAKE_BUILD_TYPE=Release'])
        run([cmake,'--build',build,'--target','wa2_fp_tests','wa2_simd_tests','wa2_fp_execution_tests','wa2_fp_workload_tests','--parallel','1'])
        run([build/'wa2_fp_tests.exe',ROOT/'logs/fp-kernel-tests.json'],30)
        run([build/'wa2_simd_tests.exe',ROOT/'logs/simd-kernel-tests.json'],30)
        run([build/'wa2_fp_execution_tests.exe',ROOT/'logs/fp-execution-tests.json'],30)
        run([build/'wa2_fp_workload_tests.exe',ROOT/'logs/fp-workload-tests.json'],30)
    print('PASS portable floating-point kernel; no game or screen access.')


if __name__=='__main__':main()
