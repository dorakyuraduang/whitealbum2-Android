"""Compare bounded generated RAM streams with a previously saved decoder.

The baseline source/header must be saved before changing the engine. Both
versions compile with O3, sequentially below normal priority. Only its namespace
is renamed; the baseline is never linked into a delivered game library.
"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline-dir', type=Path, required=True)
    parser.add_argument('--baseline-release', required=True)
    args = parser.parse_args()
    folder = args.baseline_dir.resolve()
    if not folder.is_relative_to(ROOT/'build'):
        raise ValueError('Baseline must be inside the workspace build directory')
    previous = json.loads((folder/'before-shared-aot-verification.json').read_bytes())
    if not previous['passed']:
        raise ValueError('Baseline was not verified')
    for name in ('resource_archive.cpp', 'resource_archive.hpp'):
        if digest(folder/name) != previous['source_sha256']['engine/'+name]:
            raise ValueError('Baseline bytes do not match their previous verification: '+name)
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.GetCurrentProcess.restype = ctypes.c_void_p
    kernel.SetPriorityClass.argtypes = [ctypes.c_void_p, ctypes.c_uint]
    if not kernel.SetPriorityClass(kernel.GetCurrentProcess(), 0x4000):
        raise ctypes.WinError(ctypes.get_last_error())
    kernel.SetErrorMode(0x8003)
    flags = subprocess.CREATE_NO_WINDOW | subprocess.BELOW_NORMAL_PRIORITY_CLASS
    env = os.environ.copy()
    env.update(CMAKE_BUILD_PARALLEL_LEVEL='1', TEMP=str(ROOT/'build/temp'), TMP=str(ROOT/'build/temp'),
               ZIG_GLOBAL_CACHE_DIR=str(ROOT/'build/zig-cache'), ZIG_LOCAL_CACHE_DIR=str(ROOT/'build/zig-local'))
    zig = ROOT/'tools/python/ziglang/zig.exe'

    def run(command):
        with (folder/'build.log').open('ab') as log:
            result = subprocess.run(list(map(str, command)), cwd=ROOT, env=env, creationflags=flags,
                                    stdout=log, stderr=subprocess.STDOUT, timeout=240)
        if result.returncode:
            print((folder/'build.log').read_text(errors='replace')[-6000:], flush=True)
            result.check_returncode()

    objects = []
    for version, source in [('before', folder/'resource_archive.cpp'), ('after', ROOT/'engine/resource_archive.cpp')]:
        target = folder/(version+'.o')
        run([zig, 'c++', '-target', 'x86_64-windows-gnu', '-std=c++20', '-O3', '-c', source, '-o', target,
             *(['-Dwa2=wa2_lzss_before'] if version == 'before' else [])])
        objects.append(target)
    program = folder/'compare.exe'
    run([zig, 'c++', '-target', 'x86_64-windows-gnu', '-std=c++20', '-O2', '-DWA2_LZSS_BASELINE=1',
         ROOT/'tests/lzss_groups.cpp', *objects, '-o', program])
    result_path = folder/'comparison.json'
    run([program, result_path])
    result = json.loads(result_path.read_bytes())
    if not result['passed']:
        raise ValueError('Decoder output/failure comparison failed')
    for row in result['timings']:
        row['median_paired_speedup'] = statistics.median(b/a for b, a in zip(row['before_ns'], row['ns']))
    result.update(baseline_release=args.baseline_release,
                  after_release=json.loads((ROOT/'config/release.json').read_bytes())['name'],
                  scope='Generated in-memory decompression; not PAK I/O, whole fast-forward or phone FPS',
                  source_sha256={str(p.relative_to(ROOT).as_posix()): digest(p) for p in
                                 (Path(__file__), ROOT/'tests/lzss_groups.cpp', ROOT/'engine/resource_archive.cpp',
                                  ROOT/'engine/resource_archive.hpp', folder/'resource_archive.cpp', folder/'resource_archive.hpp')},
                  baseline_verification_sha256=digest(folder/'before-shared-aot-verification.json'),
                  program_sha256=digest(program), original_executable_opened=False, android_device_verified=False)
    result_path.write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(dict(exact_cases=result['exact_output_dictionary_flags_and_tail_cases'],
                          failure_cases=result['unchanged_failure_cases'],
                          components=[dict(literal_mask=row['literal_mask'], distance=row['distance'],
                                           median_paired_speedup=row['median_paired_speedup']) for row in result['timings']]), indent=2))


if __name__ == '__main__':
    main()
