"""Compile independent engine/platform modules for ARM64, without Windows."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
ROOT=Path(__file__).resolve().parents[1]
SOURCES=['engine/pcm_audio.cpp','engine/pcm_stream.cpp','engine/resource_archive.cpp','engine/vorbis_stream.cpp','engine/ogg_reader.cpp','engine/wave_reader.cpp','engine/stream_voice.cpp','platform/file_io.cpp','platform/clock.cpp','platform/timer_resolution.cpp']
HEADERS=['engine/pcm_audio.hpp','engine/resource_archive.hpp','engine/vorbis_stream.hpp','third_party/stb/stb_vorbis.c','platform/file_io.hpp','platform/clock.hpp']
def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def main():
    output=ROOT/'build/portable-engine';output.mkdir(parents=True,exist_ok=True)
    compiler=ROOT/'tools/python/ziglang/zig.exe';records=[]
    inputs={name:digest(ROOT/name) for name in SOURCES+HEADERS+['tests/vorbis_portable.cpp']}
    environment=os.environ.copy()
    environment.update(ZIG_GLOBAL_CACHE_DIR=str(ROOT/'build/zig-cache'),ZIG_LOCAL_CACHE_DIR=str(ROOT/'build/zig-local-arm-resources'),
        TEMP=str(ROOT/'build/temp'),TMP=str(ROOT/'build/temp'))
    for name in SOURCES:
        target=output/(Path(name).stem+'-arm64.o')
        command=[str(compiler),'c++','-target','aarch64-linux-musl','-std=c++20','-O2',
            '-I'+str(ROOT/'engine'),'-I'+str(ROOT/'platform'),'-c',str(ROOT/name),'-o',str(target)]
        subprocess.run(command,cwd=ROOT,env=environment,check=True)
        data=target.read_bytes()
        if data[:5]!=b'\x7fELF\x02' or int.from_bytes(data[18:20],'little')!=183:raise ValueError('Not an ARM64 ELF object')
        records.append(dict(source=name,object=str(target),object_sha256=digest(target),bytes=len(data),command=command))
    if inputs!={name:digest(ROOT/name) for name in inputs}:raise ValueError('Sources changed during compilation')
    log=ROOT/'logs/portable-native-tests.log'
    if '100% tests passed' not in log.read_text() or 'out of 3' not in log.read_text():raise ValueError('Independent native tests must pass')
    vorbis_report=ROOT/'build/portable-native/native-vorbis-tests.json'
    vorbis=json.loads(vorbis_report.read_text());assert vorbis['passed'] and vorbis['metadata_cases']==351
    assert vorbis['audio_archives']==3 and vorbis['identification_headers']>=38844
    vorbis_program=ROOT/'build/portable-native/wa2_vorbis_stream_tests.exe'
    proof=dict(schema='wa2.portable-engine.v1',target='aarch64-linux-musl',objects=records,source_sha256=inputs,
        native_tests=dict(file=str(log),sha256=digest(log)),
        standalone_vorbis=dict(report=dict(file=str(vorbis_report),sha256=digest(vorbis_report)),
            program=dict(file=str(vorbis_program),sha256=digest(vorbis_program)),identification_headers=vorbis['identification_headers'],audio_archives=vorbis['audio_archives']),
        standalone_cmake=dict(file=str(ROOT/'engine/CMakeLists.txt'),sha256=digest(ROOT/'engine/CMakeLists.txt')),
        standalone_platform_cmake=dict(file=str(ROOT/'platform/portable.cmake'),sha256=digest(ROOT/'platform/portable.cmake')),
        windows_sdk_required=False,legacy_runtime_linked=False,android_link_or_device_test=False,passed=True)
    (ROOT/'logs/portable-engine-verification.json').write_text(json.dumps(proof,indent=2)+'\n')
    print('PASS: six native modules compiled for ARM64; three independent native tests passed. Android runtime is not verified.')
if __name__=='__main__':main()
