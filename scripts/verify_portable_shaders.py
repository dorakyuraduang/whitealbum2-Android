"""Assemble, translate and offline-link real GLSL pipelines without a GPU."""
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
    kernel=ctypes.WinDLL('kernel32');kernel.GetCurrentProcess.restype=ctypes.c_void_p;kernel.SetPriorityClass.argtypes=[ctypes.c_void_p,ctypes.c_uint]
    if not kernel.SetPriorityClass(kernel.GetCurrentProcess(),0x4000):raise ctypes.WinError(ctypes.get_last_error())
    env=os.environ.copy();env.update(TEMP=str(ROOT/'build/temp'),TMP=str(ROOT/'build/temp'),ZIG_GLOBAL_CACHE_DIR=str(ROOT/'build/zig-cache'),ZIG_LOCAL_CACHE_DIR=str(ROOT/'build/zig-local'),CMAKE_BUILD_PARALLEL_LEVEL='1')
    flags=subprocess.BELOW_NORMAL_PRIORITY_CLASS|subprocess.CREATE_NO_WINDOW
    x64=ROOT/'build/portable-shaders-x64';arm=ROOT/'build/portable-shaders-arm64';x64.mkdir(parents=True,exist_ok=True);arm.mkdir(parents=True,exist_ok=True)
    source=ROOT/'tests/portable-shaders';cmake=ROOT/'tools/python/cmake/data/bin/cmake.exe';ndk=ROOT/'tools/android-sdk/ndk/27.0.12077973';acmake=ROOT/'tools/android-sdk/cmake/3.22.1/bin/cmake.exe'
    tracked=[p for directory in ('platform/render','generated/graphics-abi','tests/portable-shaders') for p in (ROOT/directory).glob('*') if p.is_file()]
    tracked += [ROOT/'tests/portable_shaders.cpp',Path(__file__),ROOT/'scripts/generate_graphics_abi.py']
    for directory in ('tools/mojoshader-main','tools/mojoshader_assembler-main'):
        tracked += [p for p in (ROOT/directory).glob('*') if p.suffix in ('.c','.h')]
    tracked += list((ROOT/'tools/mojoshader-main/profiles').glob('*.c'))
    tool_record=ROOT/'tools/glslang/16.6.0/tool.json';tool=json.loads(tool_record.read_text())
    validator=(ROOT/tool['file']).resolve()
    if not validator.is_relative_to(ROOT/'tools/glslang') or digest(validator)!=tool['sha256']:raise ValueError('Offline GLSL validator checksum mismatch')
    tracked += [tool_record,validator,ROOT/'scripts/install_glsl_validator.py']
    hashes={p.relative_to(ROOT).as_posix():digest(p) for p in tracked}
    with (ROOT/'logs/portable-shader-build.log').open('wb') as console:
        def run(command,timeout=180):
            with subprocess.Popen(list(map(str,command)),cwd=ROOT,env=env,creationflags=flags,stdout=console,stderr=subprocess.STDOUT) as child:
                try:status=child.wait(timeout)
                except subprocess.TimeoutExpired:
                    subprocess.run(['taskkill','/PID',str(child.pid),'/T','/F'],creationflags=flags,stdout=console,stderr=subprocess.STDOUT,timeout=20);child.wait(20);raise
                if status:raise subprocess.CalledProcessError(status,command)
        run([cmake,'-S',source,'-B',x64,'-G','Ninja','-DCMAKE_TOOLCHAIN_FILE='+str(ROOT/'scripts/zig-toolchain.cmake'),'-DCMAKE_BUILD_TYPE=MinSizeRel','-DCMAKE_EXPORT_COMPILE_COMMANDS=ON'])
        run([cmake,'--build',x64,'--target','wa2_portable_shader_tests','--parallel','1'],900)
        run([x64/'wa2_portable_shader_tests.exe',x64/'checks.json',x64/'translated'],60)
        checks=json.loads((x64/'checks.json').read_text());sources=checks.pop('translated_sources')
        vertices={item['profile']:item for item in sources if not item['pixel']}
        if len(vertices)!=2 or len(sources)!=checks['desktop_and_gles_translations']:raise ValueError('Incomplete translated shader manifest')
        translated=(x64/'translated').resolve()
        def shader_file(item):
            path=(translated/item['file']).resolve()
            if not path.is_relative_to(translated) or not path.is_file():raise ValueError('Invalid translated shader path')
            return path
        pairs=0
        for item in sources:
            if not item['pixel']:continue
            # Plain -l validates the actual GLSL 120 / ES 300 source and stage
            # interface. No SPIR-V conversion or graphics context is involved.
            run([validator,'-l',shader_file(vertices[item['profile']]),shader_file(item)],30);pairs+=1
        checks.update(offline_glsl_compile_and_link_verified=True,offline_linked_programs=pairs,
                      offline_profiles=sorted(vertices),offline_validator_version=tool['version'])
        run([acmake,'-S',source,'-B',arm,'-G','Ninja','-DCMAKE_MAKE_PROGRAM='+str(ROOT/'tools/android-sdk/cmake/3.22.1/bin/ninja.exe'),'-DCMAKE_TOOLCHAIN_FILE='+str(ndk/'build/cmake/android.toolchain.cmake'),'-DANDROID_ABI=arm64-v8a','-DANDROID_PLATFORM=android-26','-DANDROID_STL=c++_shared','-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON','-DCMAKE_BUILD_TYPE=MinSizeRel','-DCMAKE_EXPORT_COMPILE_COMMANDS=ON'])
        run([acmake,'--build',arm,'--target','wa2_portable_shader_link_check','--parallel','1'],900)
    for name,value in hashes.items():
        if digest(ROOT/name)!=value:raise ValueError('shader source changed during checks: '+name)
    def compiled(directory):return sorted(str(Path(v['file']).resolve()).lower() for v in json.loads((directory/'compile_commands.json').read_text()) if '.dir' in v.get('command','') and Path(v['file']).name not in ('portable_shaders.cpp','entry.cpp'))
    if compiled(x64)!=compiled(arm):raise ValueError('shader source set differs by architecture')
    reader=ndk/'toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-readelf.exe';library=arm/'libwa2_portable_shader_link_check.so'
    elf=subprocess.run([str(reader),'--wide','-h','-d','-l',str(library)],creationflags=flags,capture_output=True,text=True,timeout=15,check=True).stdout
    if 'AArch64' not in elf or '.dll' in elf.lower():raise ValueError('wrong shader library architecture or dependency')
    proof=dict(schema='wa2.portable-shader-verification.v1',source_sha256=hashes,checks=checks,same_shared_shader_sources=True,arm64_link_verified=True,graphics_device_created=False,gpu_shader_compile_verified=False,screen_access=False,game_launched=False,source_executable_opened=False,passed=True)
    (ROOT/'logs/portable-shader-verification.json').write_text(json.dumps(proof,indent=2)+'\n');print(f'PASS {pairs} common desktop/GLES programs compiled and linked offline, ARM64 native link; no graphics device.')


if __name__=='__main__':main()
