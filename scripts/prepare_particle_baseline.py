"""Link the current bounded particle fixture to the verified 0.2.19 archives.

The saved test-only archives are never linked into either production host.
"""
import ctypes
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[1]
DIRECTORY=ROOT/'build/performance-0.2.20'


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    kernel=ctypes.WinDLL('kernel32');kernel.GetCurrentProcess.restype=ctypes.c_void_p
    kernel.SetPriorityClass.argtypes=[ctypes.c_void_p,ctypes.c_uint]
    if not kernel.SetPriorityClass(kernel.GetCurrentProcess(),0x4000):raise ctypes.WinError()
    kernel.SetErrorMode(0x8003)
    before=json.loads((DIRECTORY/'before-shared-aot-verification.json').read_text(encoding='utf-8'))
    if not before.get('passed'):raise ValueError('Missing verified prior common-code release')
    for name in ('recompiled/functions.hpp','recompiled/process.hpp','tools/WindowsRecompTool-main/include/winrecomp/process.hpp',
                 'tools/WindowsRecompTool-main/include/winrecomp/memory.hpp','tools/WindowsRecompTool-main/include/winrecomp/win32_state.hpp'):
        if sha(ROOT/name)!=before['source_sha256'][name]:raise ValueError('Saved particle library ABI changed: '+name)
    receipt=json.loads((DIRECTORY/'weather-baseline-link.json').read_text(encoding='utf-8'))
    if receipt.get('baseline_release')!='0.2.19-shared-aot':raise ValueError('Wrong particle baseline release')
    for name,value in receipt['sha256'].items():
        if sha(ROOT/name)!=value:raise ValueError('Saved common archive changed: '+name)
    archives=[value for name,value in receipt['sha256'].items() if Path(name).name=='libwa2_shared_aot.a']
    if archives!=[before['windows_library']['sha256']]:raise ValueError('Particle baseline does not match the verified prior AOT archive')
    source=ROOT/'tests/weather_cpu.cpp';exe=DIRECTORY/'weather-before.exe'
    binding=hashlib.sha256((sha(source)+json.dumps(receipt,sort_keys=True)).encode()).hexdigest()
    stamp=DIRECTORY/'weather-baseline-build.json'
    if stamp.exists():
        old=json.loads(stamp.read_text(encoding='utf-8'))
        if old.get('binding')==binding and exe.is_file() and sha(exe)==old.get('binary_sha256'):return
    flags=subprocess.CREATE_NO_WINDOW|subprocess.BELOW_NORMAL_PRIORITY_CLASS
    environment=os.environ.copy();environment.update(TEMP=str(ROOT/'build/temp'),TMP=str(ROOT/'build/temp'),
        ZIG_GLOBAL_CACHE_DIR=str(ROOT/'build/zig-cache'),ZIG_LOCAL_CACHE_DIR=str(ROOT/'build/zig-local'))
    includes=[ROOT/d for d in ('recompiled','tools/WindowsRecompTool-main/include','engine','platform','platform/render','tools/SDL-release-2.32.10/include')]
    zig=ROOT/'tools/python/ziglang/zig.exe';obj=DIRECTORY/'weather-before.obj'
    with (ROOT/'logs/particle-before-build.log').open('wb') as log:
        commands=[
            [zig,'c++','-target','x86_64-windows-gnu','-Os','-DNDEBUG','-std=c++20','-DWA2_AOT_DIAGNOSTICS=1','-DWA2_STATIC_FUNCTIONS_ONLY=1',
             *[v for directory in includes for v in ('-I',directory)],'-c',source,'-o',obj],
            [zig,'c++','-target','x86_64-windows-gnu','-Os',obj,'-o',exe,*receipt['link']]]
        for command in commands:
            subprocess.run(list(map(str,command)),cwd=ROOT,env=environment,creationflags=flags,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=180)
    stamp.write_text(json.dumps(dict(binding=binding,binary_sha256=sha(exe),source_sha256=sha(source),
        baseline_release=receipt['baseline_release'],original_executable_opened=False,game_launched=False),indent=2)+'\n',encoding='utf-8')
    print('PASS memory-only particle baseline linked to verified 0.2.19 common archives')


if __name__=='__main__':main()
