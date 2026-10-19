"""Original scene background through native modules into an owned memory surface."""
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[1]


def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    directory=ROOT/'build/native-canvas';directory.mkdir(parents=True,exist_ok=True)
    environment=os.environ.copy();environment.update(ZIG_GLOBAL_CACHE_DIR=str(ROOT/'build/zig-cache'),ZIG_LOCAL_CACHE_DIR=str(ROOT/'build/zig-local'),TEMP=str(ROOT/'build/temp'),TMP=str(ROOT/'build/temp'),CMAKE_BUILD_PARALLEL_LEVEL='1')
    flags=(subprocess.BELOW_NORMAL_PRIORITY_CLASS|subprocess.CREATE_NO_WINDOW) if os.name=='nt' else 0
    inputs=[*list((ROOT/'engine').glob('*.hpp')),*list((ROOT/'engine').glob('*.cpp')),
        *[ROOT/p for p in ('engine/CMakeLists.txt','platform/sdl_scene.hpp','platform/sdl_scene.cpp',
            'platform/sdl_canvas.hpp','platform/sdl_canvas.cpp','platform/sdl_canvas.cmake','tests/canvas/CMakeLists.txt',
            'tests/native_background.cpp','scripts/verify_native_background.py')]]
    bindings={p.relative_to(ROOT).as_posix():digest(p) for p in inputs}
    cmake=ROOT/'tools/python/cmake/data/bin/cmake.exe';arms=[]
    with (ROOT/'logs/native-background-build.log').open('wb') as console:
        for command in ([str(cmake),'-S',str(ROOT/'tests/canvas'),'-B',str(directory),'-G','Ninja',
            '-DCMAKE_TOOLCHAIN_FILE='+str(ROOT/'scripts/zig-toolchain.cmake'),'-DCMAKE_BUILD_TYPE=Release'],
            [str(cmake),'--build',str(directory),'--target','wa2_background_tests','--parallel','1']):
            subprocess.run(command,cwd=ROOT,env=environment,creationflags=flags,stdout=console,stderr=subprocess.STDOUT,timeout=900,check=True)
        for source in ('engine/image_store.cpp','engine/scene_background.cpp','platform/sdl_scene.cpp'):
            path=ROOT/source;arm=directory/(path.stem+'-aarch64.o');arms.append(arm)
            subprocess.run([str(ROOT/'tools/python/ziglang/zig.exe'),'c++','-std=c++20','-O1','-g0','-target','aarch64-linux-musl',
                '-I'+str(ROOT/'engine'),'-I'+str(ROOT/'platform'),'-I'+str(ROOT/'tools/SDL-release-2.32.10/include'),
                '-c',str(path),'-o',str(arm)],cwd=ROOT,env=environment,creationflags=flags,stdout=console,stderr=subprocess.STDOUT,timeout=120,check=True)
    program=directory/'wa2_background_tests.exe';result=directory/'native-background-tests.json'
    with (ROOT/'logs/native-background-tests.log').open('wb') as console:
        subprocess.run([str(program),str(ROOT/'res'),str(result)],cwd=ROOT,creationflags=flags,stdout=console,stderr=subprocess.STDOUT,timeout=15,check=True)
    for name,value in bindings.items():
        if digest(ROOT/name)!=value:raise ValueError('Background verification source changed: '+name)
    proof=json.loads(result.read_text());proof.update(source_sha256=bindings,test_program_sha256=digest(program),
        arm64_target='aarch64-linux-musl',arm64_objects_sha256={p.name:digest(p) for p in arms},
        android_ndk_link_verified=False,android_device_verified=False,published_runtime_changed=False)
    (ROOT/'logs/native-background-verification.json').write_text(json.dumps(proof,indent=2)+'\n')
    print('PASS native scene background; next command',proof['next_unimplemented_command'],proof['next_unimplemented_command_name'],
        '(memory surface only; not a playable Android APK).')


if __name__=='__main__':main()
