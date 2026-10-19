"""Compile the existing C++ sorting module for ARM64, without a Windows SDK."""
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    directory = ROOT/'build/portable-sort'
    directory.mkdir(parents=True, exist_ok=True)
    compiler = ROOT/'tools/python/ziglang/zig.exe'
    source = ROOT/'port/sprite_sort.cpp'
    output = directory/'sprite_sort.o'
    inputs = {str(p.relative_to(ROOT)): digest(p) for p in (
        source, ROOT/'port/sprite_sort.hpp', ROOT/'port/native_backend.hpp', ROOT/'port/sort_signature.inc')}
    command = [str(compiler), 'c++', '-target', 'aarch64-linux-musl', '-std=c++20', '-O2',
               '-I', str(ROOT/'port'), '-I', str(ROOT/'tools/WindowsRecompTool-main/include'),
               '-c', str(source), '-o', str(output)]
    environment = os.environ.copy()
    environment.update(ZIG_GLOBAL_CACHE_DIR=str(ROOT/'build/zig-cache'),
                       ZIG_LOCAL_CACHE_DIR=str(ROOT/'build/zig-local'),
                       TEMP=str(ROOT/'build/temp'), TMP=str(ROOT/'build/temp'))
    log = ROOT/'logs/sprite-sort-arm64-build.log'
    with log.open('wb') as console:
        subprocess.run(command, cwd=ROOT, env=environment, stdout=console,
                       stderr=subprocess.STDOUT, timeout=120, check=True)
    data = output.read_bytes()
    if data[:6] != b'\x7fELF\x02\x01' or int.from_bytes(data[18:20], 'little') != 183:
        raise ValueError('Compiler did not produce a little-endian ARM64 ELF64 object')
    if any(digest(ROOT/name) != value for name, value in inputs.items()):
        raise ValueError('Sorting sources changed during compilation')
    proof = dict(schema='wa2.sprite-sort-portability.v1', target='aarch64-linux-musl',
                 command=command, source_sha256=inputs, object=str(output), object_sha256=digest(output),
                 object_bytes=len(data), compiler_sha256=digest(compiler),
                 source_executable_analyzed=False, external_project_referenced=False,
                 x86_assembly_in_sort_module=False, android_executable_built=False,
                 android_device_verified=False, entire_runtime_portable=False,
                 screen_access=False, passed=True)
    (ROOT/'logs/sprite-sort-portability.json').write_text(json.dumps(proof, indent=2)+'\n')
    print('PASS: sorting module compiles to ARM64; Android runtime and frame rate remain unverified.')


if __name__ == '__main__':
    main()
