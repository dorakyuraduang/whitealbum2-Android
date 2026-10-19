"""Build/run only owned-memory dialogue checks, one low-priority compiler job."""
import ctypes
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[1]


def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    if os.name=='nt':ctypes.windll.kernel32.SetPriorityClass(ctypes.windll.kernel32.GetCurrentProcess(),0x4000)
    build=ROOT/'build/native-canvas'
    inputs=[p for folder in ('engine','platform') for p in (ROOT/folder).glob('*') if p.suffix in ('.cpp','.hpp','.inc','.cmake','.txt')]
    inputs +=[Path(__file__),ROOT/'tests/native_dialogue.cpp',ROOT/'tests/canvas/CMakeLists.txt']
    bindings={p.relative_to(ROOT).as_posix():digest(p) for p in inputs}
    environment=os.environ.copy();environment.update(ZIG_GLOBAL_CACHE_DIR=str(ROOT/'build/zig-cache'),ZIG_LOCAL_CACHE_DIR=str(ROOT/'build/zig-local'),
        TEMP=str(ROOT/'build/temp'),TMP=str(ROOT/'build/temp'),CMAKE_BUILD_PARALLEL_LEVEL='1')
    flags=subprocess.BELOW_NORMAL_PRIORITY_CLASS|subprocess.CREATE_NO_WINDOW
    cmake=ROOT/'tools/python/cmake/data/bin/cmake.exe'
    with (ROOT/'logs/native-dialogue-build.log').open('wb') as console:
        for command in ([str(cmake),'-S',str(ROOT/'tests/canvas'),'-B',str(build),'-G','Ninja',
            '-DCMAKE_TOOLCHAIN_FILE='+str(ROOT/'scripts/zig-toolchain.cmake'),'-DCMAKE_BUILD_TYPE=Release','-DWA2_TEST_MOVIE=ON'],
            [str(cmake),'--build',str(build),'--target','wa2_dialogue_tests','--parallel','1']):
            subprocess.run(command,cwd=ROOT,env=environment,creationflags=flags,stdout=console,stderr=subprocess.STDOUT,check=True,timeout=900)
    report=build/'native-dialogue-tests.json';program=build/'wa2_dialogue_tests.exe'
    with (ROOT/'logs/native-dialogue-tests.log').open('wb') as console:
        subprocess.run([str(program),str(ROOT/'res'),str(report)],cwd=ROOT,creationflags=flags,stdout=console,stderr=subprocess.STDOUT,timeout=20,check=True)
    for name,value in bindings.items():
        if digest(ROOT/name)!=value:raise ValueError('Dialogue input changed: '+name)
    proof=json.loads(report.read_text());proof.update(source_sha256=bindings,test_program_sha256=digest(program),
        android_device_verified=False,whole_gameplay_verified=False,apk_created=False)
    (ROOT/'logs/native-dialogue-verification.json').write_text(json.dumps(proof,indent=2)+'\n')
    print('PASS native dialogue/font/window memory checks; no game/window/audio device was opened.')


if __name__=='__main__':main()
