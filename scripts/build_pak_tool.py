"""Build the standalone Windows PAK GUI and run file/codec component checks.

No game inputs, generated functions, Android builds, or gameplay are required.
"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
VERSION = '1.0.0'


def low_priority():
    if os.name != 'nt':
        raise SystemExit('The desktop PAK tool currently targets Windows x64.')
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.GetCurrentProcess.restype = ctypes.c_void_p
    kernel.SetPriorityClass.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
    kernel.GetProcessAffinityMask.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_size_t)]
    kernel.SetProcessAffinityMask.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
    process = kernel.GetCurrentProcess()
    if not kernel.SetPriorityClass(process, 0x4000):
        raise ctypes.WinError(ctypes.get_last_error())
    available, system = ctypes.c_size_t(), ctypes.c_size_t()
    if not kernel.GetProcessAffinityMask(process, ctypes.byref(available), ctypes.byref(system)):
        raise ctypes.WinError(ctypes.get_last_error())
    if not kernel.SetProcessAffinityMask(process, available.value & -available.value):
        raise ctypes.WinError(ctypes.get_last_error())


def main():
    # Compiler diagnostics and fixture names contain non-GBK characters.
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    sys.stderr.reconfigure(encoding='utf-8', errors='replace')
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'build/pak-tool')
    parser.add_argument('--output', type=Path, default=ROOT / 'out/pak-tool')
    parser.add_argument('--cmake', type=Path, default=ROOT / 'tools/python/cmake/data/bin/cmake.exe')
    args = parser.parse_args()
    low_priority()
    build = args.build_dir.resolve()
    output = args.output.resolve()
    build.mkdir(parents=True, exist_ok=True)
    environment = os.environ.copy()
    environment['CMAKE_BUILD_PARALLEL_LEVEL'] = '1'
    # Reuse the existing local compiler runtime cache rather than rebuilding it.
    warm_cache = ROOT / 'build/test-content-046/zig-cache'
    environment.setdefault('ZIG_GLOBAL_CACHE_DIR', str(warm_cache if warm_cache.is_dir() else build / 'zig-cache'))
    environment.setdefault('ZIG_LOCAL_CACHE_DIR', str(build / 'zig-local'))
    flags = subprocess.CREATE_NO_WINDOW | subprocess.BELOW_NORMAL_PRIORITY_CLASS

    def run(arguments):
        result = subprocess.run([str(value) for value in arguments], cwd=ROOT, env=environment, creationflags=flags,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding='utf-8', errors='replace')
        with (build / 'build.log').open('a', encoding='utf-8') as log:
            log.write(result.stdout)
        print(result.stdout, end='', flush=True)
        result.check_returncode()

    run([args.cmake, '-S', ROOT / 'tools/wa2-pak-tool', '-B', build, '-G', 'Ninja',
         '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_TOOLCHAIN_FILE=' + str(ROOT / 'scripts/zig-toolchain.cmake')])
    run([args.cmake, '--build', build, '--parallel', '1'])
    run([build / 'wa2_pak_tests.exe', build])
    data = (build / 'WA2_PAK_Tool.exe').read_bytes()
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    if (data[:2] != b'MZ' or data[pe:pe+4] != b'PE\0\0' or
            struct.unpack_from('<H', data, pe+4)[0] != 0x8664 or
            struct.unpack_from('<H', data, pe+24)[0] != 0x20b or
            struct.unpack_from('<H', data, pe+24+68)[0] != 2):
        raise RuntimeError('Expected an AMD64 Windows GUI executable (without a console window)')
    output.mkdir(parents=True, exist_ok=True)
    exe = output / 'WA2_PAK_Tool.exe'
    shutil.copy2(build / exe.name, exe)
    shutil.copy2(ROOT / 'docs/PAK_TOOL.md', output / 'README.md')
    shutil.copy2(ROOT / 'LICENSE', output / 'LICENSE.txt')
    bundle = output / f'WA2-PAK-Tool-{VERSION}-win64.zip'
    with zipfile.ZipFile(bundle, 'w', zipfile.ZIP_DEFLATED) as archive:
        for name in ('WA2_PAK_Tool.exe', 'README.md', 'LICENSE.txt'):
            archive.write(output / name, name)
    hashes = {path.name: hashlib.sha256(path.read_bytes()).hexdigest() for path in (exe, bundle)}
    (output / 'SHA256SUMS.txt').write_text(''.join(f'{digest}  {name}\n' for name, digest in hashes.items()), encoding='ascii')
    report = dict(version=VERSION, executable=str(exe), executable_bytes=exe.stat().st_size,
                  hashes=hashes, component_checks='passed', pe_machine='AMD64', pe_subsystem='Windows GUI',
                  build_jobs=1, game_launched=False, screen_read=False)
    (build / 'verification.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
