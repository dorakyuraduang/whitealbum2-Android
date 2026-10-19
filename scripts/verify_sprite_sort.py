"""Bind the local sorting comparisons, candidate and ARM64 compile evidence."""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    before_file = ROOT/'logs/sprite-sort-baseline.json'
    after_file = ROOT/'logs/sprite-sort-optimized.json'
    before, after = (json.loads(p.read_text()) for p in (before_file, after_file))
    if before['partial_search_enabled'] or not after['partial_search_enabled']:
        raise ValueError('Sorting comparison does not use the expected two paths')
    if before['program_sha256'] != after['program_sha256'] or before['source_sha256'] != after['source_sha256']:
        raise ValueError('Sorting comparisons used different binaries or sources')
    if digest(ROOT/'build/wa2-win64/wa2_port/wa2_existing_sort_tests.exe') != after['program_sha256']:
        raise ValueError('Test executable changed after comparison')
    for record in (before, after):
        if record['cases'] < 603 or not record['exact_cpu_flags_fpu_memory_budget_faults']:
            raise ValueError('Incomplete generated-code conformance tests')
        for name, value in record['source_sha256'].items():
            if digest(ROOT/name) != value:
                raise ValueError('Source changed after testing: '+name)
        if record['screen_access'] or record['original_executable_opened']:
            raise ValueError('Unexpected test scope')
    earlier = {(r['active_objects'], r['slice']):r['median_ms_per_sort'] for r in before['benchmarks']}
    comparisons = []
    for row in after['benchmarks']:
        key = row['active_objects'], row['slice']
        old, new = earlier[key], row['median_ms_per_sort']
        comparisons.append(dict(active_objects=key[0], slice=key[1], before_ms=old, after_ms=new,
                                time_reduction_percent=100*(1-new/old), speedup=old/new))
    for count in (128, 2048):
        row = next(r for r in comparisons if r['active_objects']==count and r['slice']==32768)
        if row['time_reduction_percent'] < 20:
            raise ValueError('Dense sorting improvement did not reproduce')
    portable_file = ROOT/'logs/sprite-sort-portability.json'
    portable = json.loads(portable_file.read_text())
    if not portable['passed'] or portable['target'] != 'aarch64-linux-musl':
        raise ValueError('Missing ARM64 module compile evidence')
    for name, value in portable['source_sha256'].items():
        if digest(ROOT/name) != value:
            raise ValueError('Source changed after ARM64 compilation')
    if digest(Path(portable['object'])) != portable['object_sha256']:
        raise ValueError('ARM64 object changed after compilation')
    metadata = json.loads((ROOT/'out/build-info.json').read_text())
    published = {name:digest(ROOT/name) for name in ('out/WA2_SDL2_x64.exe','res/WA2_SDL2_x64.exe')}
    if any(value != metadata['output_sha256'] for value in published.values()):
        raise ValueError('Published runtime changed; update its provenance before verification')
    candidate = ROOT/'build/wa2-win64/candidate/WA2_SDL2_x64.exe'
    previous = ROOT/'build/existing-code-tests/phase-before/WA2_SDL2_x64.exe'
    proof = dict(schema='wa2.sprite-sort-verification.v1', comparisons=comparisons,
                 differential_cases_per_path=after['cases'],
                 baseline_report_sha256=digest(before_file), optimized_report_sha256=digest(after_file),
                 portability_report_sha256=digest(portable_file),
                 candidate=dict(file=str(candidate), sha256=digest(candidate), bytes=candidate.stat().st_size),
                 previous_bytes=previous.stat().st_size, package_delta_bytes=candidate.stat().st_size-previous.stat().st_size,
                 published_runtime_sha256=published,
                 target=dict(original_performance_fraction=[0.7,0.8], chipset='Snapdragon 7s Gen 2',
                             frames_per_second=120, milliseconds_per_frame=1000/120, target_only=True),
                 source_executable_analyzed=False, external_project_referenced=False, screen_access=False,
                 whole_game_speedup_measured=False, android_device_verified=False, target_120fps_verified=False,
                 full_execution_suite_run=False, published_runtime_changed=False, passed=True)
    (ROOT/'logs/sprite-sort-verification.json').write_text(json.dumps(proof, indent=2)+'\n')
    print(json.dumps(dict(differential_cases_per_path=after['cases'], comparisons=comparisons,
                         package_delta_bytes=proof['package_delta_bytes'], android_device_verified=False), indent=2))


if __name__ == '__main__':
    main()
