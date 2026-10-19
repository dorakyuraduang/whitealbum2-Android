"""Verify execution optimizations without screen capture or window automation."""
from pathlib import Path
import hashlib
import argparse
import json
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--program',type=Path,default=ROOT/'out/WA2_SDL2_x64.exe');args=parser.parse_args()
    image = str(ROOT / 'WA2_chs.exe')
    from verify_retired_modules import verify as check_retired
    retired=check_retired()
    assert retired==json.loads((ROOT/'logs/retired-modules-verification.json').read_text())
    directory = ROOT / 'build/wa2-win64/wa2_port'
    fixtures = []
    names = {
        'wa2_graphics_tests': None,
        'wa2_audio_buffer_tests': 'audio-buffers-tests',
        'wa2_native_vorbis_tests': 'native-vorbis-tests',
        'wa2_resource_memory_tests': 'resource-memory-tests',
        'wa2_pcm_audio_tests': 'native-pcm-tests',
        'wa2_resource_tests': 'native-resource-tests',
        'wa2_port_tests': 'native-platform-api-tests',
        'wa2_media_tests': None,
        'wa2_win64_tests': None,
        'wa2_cpu_feature_tests': 'cpu-feature-tests',
        'wa2_device_tests': 'device-message-tests',
        'wa2_heap_churn_tests': 'heap-churn-tests',
        'wa2_pak_loading_tests': 'pak-loading-tests',
        'wa2_script_tests': 'script-handler-tests',
        'wa2_fast_tests': 'static-batch-tests',
        'wa2_block_tests': 'static-block-tests',
        'wa2_native_tests': 'native-backend-tests',
        'wa2_vorbis_tests': 'vorbis-batch-tests',
        'wa2_vorbis_butterfly_tests': 'vorbis-butterfly-tests',
        'wa2_color_tests': 'color-batch-tests',
        'wa2_vertex_tests': 'vertex-batch-tests',
        'wa2_uv_tests': 'uv-batch-tests',
        'wa2_fp_sequence_tests': 'fp-sequence-tests',
        'wa2_shift_tests': 'shift-fast-tests',
        'wa2_sprite_transform_tests': 'sprite-transform-tests',
        'wa2_bitreader_tests': 'bitreader-batch-tests',
        'wa2_thread_tests': None,
        'wa2_timing_tests': 'timing-boundary-tests',
    }
    for name, report_name in names.items():
        program = directory / ('engine' if name=='wa2_pcm_audio_tests' else '') / (name + '.exe')
        log = ROOT / 'logs' / ('execution-' + name + '.log')
        arguments = [] if name in ('wa2_shift_tests','wa2_heap_churn_tests','wa2_pcm_audio_tests') else [image]
        if name=='wa2_resource_tests':arguments=[str(ROOT/'res'),str(ROOT/'build/resource-load/portable-samples.bin')]
        if name=='wa2_native_vorbis_tests':arguments=[image,str(ROOT/'res')]
        if name=='wa2_graphics_tests':arguments.append('--offscreen-only')
        if name=='wa2_media_tests':arguments.append('--events-only')
        startup=None
        if name=='wa2_win64_tests':
            arguments.append('--startup-only')
            startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=4
        report = ROOT / 'logs' / (report_name + '.json') if report_name else None
        if report:
            arguments.append(str(report))
        if name=='wa2_pak_loading_tests':
            samples=ROOT/'build/resource-load/samples.bin'
            if not samples.exists():raise RuntimeError('Run scripts/make_resource_samples.py before PAK regressions')
            arguments.append(str(samples))
            arguments.append(str(ROOT/'build/resource-load/portable-samples.bin'))
        with log.open('wb') as console:
            result = subprocess.run([str(program), *arguments], cwd=ROOT,
                # The old codec is deliberately absent from the release AOT.
                # Its complete C++ oracle can take longer than ordinary tests.
                stdout=console, stderr=subprocess.STDOUT, timeout=600 if name=='wa2_native_vorbis_tests' else 120,startupinfo=startup)
        print(name, result.returncode, flush=True)
        if result.returncode:
            raise RuntimeError(log.read_text(encoding='utf-8', errors='replace')[-5000:])
        fixture = dict(fixture=name, return_code=0, program_sha256=digest(program),
            log=str(log), log_sha256=digest(log))
        if report:
            fixture.update(report=str(report), report_sha256=digest(report))
        fixtures.append(fixture)
    core_log = ROOT / 'logs/core-regression-tests.log'
    if '100% tests passed out of 10' not in core_log.read_text():
        raise RuntimeError('The ten non-window core regressions must pass first')
    proof = dict(schema='wa2.execution-suite.v1',
        output_sha256=digest(args.program),program_file=str(args.program.resolve()),
        port_library_sha256=digest(directory / 'libwa2_port.a'),
        native_engine_library_sha256=digest(directory / 'engine/libwa2_engine.a'),
        platform_library_sha256=digest(directory / 'libwa2_platform.a'),
        sdl_audio_library_sha256=digest(directory / 'libwa2_sdl_audio.a'),
        sdl_library_sha256=digest(directory / 'sdl2/libSDL2.a'),
        compact_library_sha256=digest(directory / 'libwa2_compact_ops.a'),
        codec_oracle_library_sha256=digest(directory / 'libwa2_codec_oracle.a'),
        retired_module_proof_sha256=digest(ROOT/'logs/retired-modules-verification.json'),
        runtime_library_sha256=digest(ROOT / 'build/wa2-win64/winrecomp/libwinrecomp_guest.a'),
        native_helpers_library_sha256=digest(ROOT / 'build/wa2-win64/winrecomp/libwinrecomp_native.a'),
        core_log=str(core_log), core_log_sha256=digest(core_log), core_tests=10,
        fixtures=fixtures, screen_access=False, gameplay_verified=False)
    (ROOT / 'logs/execution-suite.json').write_text(json.dumps(proof, indent=2) + '\n')
    print(f'All {len(fixtures)} execution fixtures passed; no screen access.', flush=True)


if __name__ == '__main__':
    main()
