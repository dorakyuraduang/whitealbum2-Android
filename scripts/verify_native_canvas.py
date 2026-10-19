"""Build a small SDL software-renderer check; never create a window or run the game."""
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[1]


def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    directory=ROOT/'build/native-canvas';directory.mkdir(parents=True,exist_ok=True)
    cmake=ROOT/'tools/python/cmake/data/bin/cmake.exe'
    environment=os.environ.copy();environment.update(ZIG_GLOBAL_CACHE_DIR=str(ROOT/'build/zig-cache'),ZIG_LOCAL_CACHE_DIR=str(ROOT/'build/zig-local'),TEMP=str(ROOT/'build/temp'),TMP=str(ROOT/'build/temp'),CMAKE_BUILD_PARALLEL_LEVEL='1')
    flags=(subprocess.BELOW_NORMAL_PRIORITY_CLASS|subprocess.CREATE_NO_WINDOW) if os.name=='nt' else 0
    inputs=[ROOT/p for p in ('platform/sdl_canvas.hpp','platform/sdl_canvas.cpp','platform/sdl_input.hpp','platform/sdl_input.cpp','platform/sdl_canvas.cmake','tests/native_canvas.cpp','tests/native_input.cpp','tests/canvas/CMakeLists.txt','scripts/verify_native_canvas.py')]
    bindings={p.relative_to(ROOT).as_posix():digest(p) for p in inputs}
    with (ROOT/'logs/native-canvas-build.log').open('wb') as console:
        commands=[
            [str(cmake),'-S',str(ROOT/'tests/canvas'),'-B',str(directory),'-G','Ninja','-DCMAKE_TOOLCHAIN_FILE='+str(ROOT/'scripts/zig-toolchain.cmake'),'-DCMAKE_BUILD_TYPE=Release'],
            [str(cmake),'--build',str(directory),'--target','wa2_canvas_tests','wa2_input_tests','--parallel','1'],
        ]
        for command in commands:
            subprocess.run(command,cwd=ROOT,env=environment,creationflags=flags,stdout=console,stderr=subprocess.STDOUT,timeout=900,check=True)
        arms=[]
        for name in ('sdl_canvas','sdl_input'):
            arm=directory/(name+'-aarch64.o');arms.append(arm)
            subprocess.run([str(ROOT/'tools/python/ziglang/zig.exe'),'c++','-std=c++20','-O1','-g0','-target','aarch64-linux-musl',
                '-I'+str(ROOT/'engine'),'-I'+str(ROOT/'platform'),'-I'+str(ROOT/'tools/SDL-release-2.32.10/include'),
                '-c',str(ROOT/'platform'/f'{name}.cpp'),'-o',str(arm)],cwd=ROOT,env=environment,creationflags=flags,stdout=console,stderr=subprocess.STDOUT,timeout=120,check=True)
    program=directory/'wa2_canvas_tests.exe';result=directory/'native-canvas-tests.json'
    with (ROOT/'logs/native-canvas-tests.log').open('wb') as console:
        subprocess.run([str(program),str(result)],cwd=ROOT,creationflags=flags,stdout=console,stderr=subprocess.STDOUT,timeout=30,check=True)
    input_program=directory/'wa2_input_tests.exe';input_result=directory/'native-input-tests.json'
    with (ROOT/'logs/native-input-tests.log').open('wb') as console:
        subprocess.run([str(input_program),str(input_result)],cwd=ROOT,creationflags=flags,stdout=console,stderr=subprocess.STDOUT,timeout=30,check=True)
    for name,value in bindings.items():
        if digest(ROOT/name)!=value:raise ValueError('Canvas verification source changed: '+name)
    proof=json.loads(result.read_text());proof.update(source_sha256=bindings,test_program_sha256=digest(program),apk_created=False,published_runtime_changed=False,
        sdl_library_sha256=digest(directory/'sdl2/libSDL2.a'),android_ndk_link_verified=False,
        arm64_target='aarch64-linux-musl',arm64_objects_sha256={p.name:digest(p) for p in arms},
        input_verification=json.loads(input_result.read_text()),input_test_program_sha256=digest(input_program))
    (ROOT/'logs/native-canvas-verification.json').write_text(json.dumps(proof,indent=2)+'\n')
    print('PASS native SDL canvas: offscreen software only; no window, game, or screen access.')


if __name__=='__main__':main()
