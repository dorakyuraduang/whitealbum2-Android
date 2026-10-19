"""Small, low-priority synthetic checks; never run the game or a stress test."""
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    directory = ROOT/'build/android-frame-metrics'
    directory.mkdir(parents=True, exist_ok=True)
    source = ROOT/'android/native/frame_metrics.cpp'
    header = ROOT/'android/native/frame_metrics.hpp'
    test = ROOT/'tests/android_frame_metrics.cpp'
    compiler = ROOT/'tools/python/ziglang/zig.exe'
    program = directory/'frame_metrics_tests.exe'
    arm_object = directory/'frame_metrics-arm64.o'
    report = directory/'frame-metrics-tests.json'
    inputs = {str(p.relative_to(ROOT)): digest(p) for p in (source, header, test)}
    environment = os.environ.copy()
    environment.update(ZIG_GLOBAL_CACHE_DIR=str(ROOT/'build/zig-cache'),
                       ZIG_LOCAL_CACHE_DIR=str(directory/'zig-cache'),
                       TEMP=str(ROOT/'build/temp'), TMP=str(ROOT/'build/temp'))
    flags = (subprocess.BELOW_NORMAL_PRIORITY_CLASS | subprocess.CREATE_NO_WINDOW) if os.name=='nt' else 0
    commands = [
        [str(compiler), 'c++', '-target', 'x86_64-windows-gnu', '-std=c++20', '-O1',
         '-I'+str(source.parent), str(source), str(test), '-o', str(program)],
        [str(program), str(report)],
        [str(compiler), 'c++', '-target', 'aarch64-linux-musl', '-std=c++20', '-O1',
         '-I'+str(source.parent), '-c', str(source), '-o', str(arm_object)],
    ]
    log = ROOT/'logs/android-frame-metrics-tests.log'
    with log.open('wb') as console:
        for command in commands:
            subprocess.run(command, cwd=ROOT, env=environment, stdout=console, stderr=subprocess.STDOUT,
                           timeout=90, check=True, creationflags=flags)
    if any(digest(ROOT/name)!=value for name,value in inputs.items()):
        raise ValueError('Frame-statistics sources changed during testing')
    result = json.loads(report.read_text())
    data = arm_object.read_bytes()
    if not result['passed'] or result['cases']<14 or data[:5]!=b'\x7fELF\x02' or int.from_bytes(data[18:20],'little')!=183:
        raise ValueError('Incomplete native or ARM64 compile checks')
    result.update(source_sha256=inputs, commands=commands, compiler_sha256=digest(compiler),
                  host_test_sha256=digest(program), arm64_object_sha256=digest(arm_object),
                  android_ndk_link_verified=False, android_java_compiled=False,
                  actual_game_present_hook_connected=False, apk_created=False)
    (ROOT/'logs/android-frame-metrics-verification.json').write_text(json.dumps(result, indent=2)+'\n')
    print('PASS: synthetic FPS/latency/lifecycle checks and ARM64 object compilation; no game or pressure test run.')


if __name__ == '__main__':
    main()
