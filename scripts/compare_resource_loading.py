"""Compare archived/current resource fixtures without accessing game windows."""
from pathlib import Path
import argparse
import hashlib
import json
import statistics
import subprocess
import uuid

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def bound(path):
    return dict(file=str(path.resolve()), sha256=digest(path))


def verification():
    proof = json.loads((ROOT/'logs/resource-loading-comparison.json').read_text())
    suite = json.loads((ROOT/'logs/execution-suite.json').read_text())
    assert proof['passed'] and not proof['screen_access'] and not proof['automated_input']
    assert not proof['original_executable_speed_measured'] and not proof['whole_scene_loading_measured']
    baseline = proof['baseline_suite']
    assert digest(baseline['file']) == baseline['sha256']
    before_suite = json.loads(Path(baseline['file']).read_text())
    for version, current_suite in [('before', before_suite), ('after', suite)]:
        for kind, name in [('resource', 'wa2_resource_tests'), ('pak', 'wa2_pak_loading_tests')]:
            program = proof['programs'][version][kind]
            fixture = next(f for f in current_suite['fixtures'] if f['fixture'] == name)
            assert digest(program['file']) == program['sha256'] == fixture['program_sha256']
        rows = [row for row in proof['observations'] if row['version'] == version]
        assert sorted(row['round'] for row in rows) == [0, 1, 2]
        for row in rows:
            for kind in ['resource', 'pak']:
                for evidence in row[kind].values():
                    assert digest(evidence['file']) == evidence['sha256']
            resource = json.loads(Path(row['resource']['report']['file']).read_text())
            pak = json.loads(Path(row['pak']['report']['file']).read_text())
            assert resource['passed'] and pak['passed'] and pak['original_image_loader_matches']
            assert resource['dictionary_reference_cases'] == 2000 and pak['native_full_image_load_cases'] == 40
            assert row['image_load_ms'] == pak['image_load_seconds']*1000
            assert row['decode'] == resource['decode_comparison']
            assert row['timing'] == pak['native_resources']['timing']
            if version == 'after':
                assert resource['fixed_copy_boundary_cases'] == 5200
                assert pak['transactional_decode_failure_cases'] == 6
                assert pak['native_resources']['single_pass_image_decodes'] == 41
        expected = dict(image_load_median_ms=statistics.median(row['image_load_ms'] for row in rows),
                        decode_median_ms=statistics.median(row['decode']['native_median_ns']/1e6 for row in rows),
                        decode_round_output_bytes=rows[0]['decode']['round_output_bytes'])
        assert proof['summary'][version] == expected
    reduction = 1-proof['summary']['after']['image_load_median_ms']/proof['summary']['before']['image_load_median_ms']
    assert proof['image_load_time_reduction_percent'] == reduction*100
    return proof


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--before-dir', type=Path, required=True)
    parser.add_argument('--evidence', type=Path, default=ROOT/'logs/resource-loading-comparison.json')
    args = parser.parse_args()
    before = args.before_dir.resolve()
    previous = json.loads((before/'execution-before.json').read_text())
    programs = {
        'before': dict(resource=before/'resource-before.exe', pak=before/'pak-before.exe'),
        'after': dict(resource=ROOT/'build/wa2-win64/wa2_port/wa2_resource_tests.exe',
                      pak=ROOT/'build/wa2-win64/wa2_port/wa2_pak_loading_tests.exe'),
    }
    for kind, name in [('resource', 'wa2_resource_tests'), ('pak', 'wa2_pak_loading_tests')]:
        fixture = next(f for f in previous['fixtures'] if f['fixture'] == name)
        if digest(programs['before'][kind]) != fixture['program_sha256']:
            raise ValueError('Archived baseline fixture differs from its verified executable')
    binaries = {version: {kind: bound(path) for kind, path in items.items()}
                for version, items in programs.items()}
    case = ROOT/'build/resource-comparison'/uuid.uuid4().hex
    case.mkdir(parents=True)
    observations = []
    for round_number in range(3):
        for version in (['before', 'after'] if round_number % 2 == 0 else ['after', 'before']):
            record = dict(round=round_number, version=version)
            for kind, program in programs[version].items():
                report = case/f'{round_number}-{version}-{kind}.json'
                log = report.with_suffix('.log')
                arguments = ([str(ROOT/'res'), str(ROOT/'build/resource-load/portable-samples.bin')]
                             if kind == 'resource' else [str(ROOT/'WA2_chs.exe')])
                arguments.append(str(report))
                if kind == 'pak':
                    arguments += [str(ROOT/'build/resource-load/samples.bin'),
                                  str(ROOT/'build/resource-load/portable-samples.bin')]
                with log.open('wb') as output:
                    subprocess.run([str(program), *arguments], cwd=ROOT, stdout=output,
                                   stderr=subprocess.STDOUT, check=True, timeout=120)
                result = json.loads(report.read_text())
                if not result['passed'] or result['screen_access']:
                    raise ValueError('Resource regression did not pass')
                if kind == 'resource':
                    assert result['dictionary_reference_cases'] == 2000
                    if version == 'after':
                        assert result['fixed_copy_boundary_cases'] == 5200
                    record['decode'] = result['decode_comparison']
                else:
                    assert result['native_full_image_load_cases'] == 40
                    assert result['reference_hashes_match'] and result['original_image_loader_matches']
                    if version == 'after':
                        assert result['transactional_decode_failure_cases'] == 6
                        assert result['native_resources']['single_pass_image_decodes'] == 41
                    record['image_load_ms'] = result['image_load_seconds'] * 1000
                    record['timing'] = result['native_resources']['timing']
                record[kind] = dict(report=bound(report), console=bound(log))
            observations.append(record)
            print(f'round {round_number + 1} {version}: 40 image loads {record["image_load_ms"]:.3f} ms', flush=True)
    for version, items in programs.items():
        for kind, program in items.items():
            if digest(program) != binaries[version][kind]['sha256']:
                raise ValueError('Fixture changed during comparison')
    summary = {}
    for version in programs:
        rows = [row for row in observations if row['version'] == version]
        summary[version] = dict(
            image_load_median_ms=statistics.median(row['image_load_ms'] for row in rows),
            decode_median_ms=statistics.median(row['decode']['native_median_ns']/1e6 for row in rows),
            decode_round_output_bytes=rows[0]['decode']['round_output_bytes'])
    reduction = 1-summary['after']['image_load_median_ms']/summary['before']['image_load_median_ms']
    proof = dict(schema='wa2.resource-loading-comparison.v1', baseline_suite=bound(before/'execution-before.json'),
                 programs=binaries, observations=observations, summary=summary,
                 image_load_time_reduction_percent=reduction*100,
                 scope='40 read/decode/allocate image loads of five real assets; three alternating rounds; file cache not flushed',
                 original_executable_speed_measured=False, whole_scene_loading_measured=False,
                 android_device_verified=False, screen_access=False, automated_input=False, passed=True)
    args.evidence.write_text(json.dumps(proof, indent=2)+'\n')
    print(json.dumps(dict(summary=summary, image_load_time_reduction_percent=reduction*100), indent=2))


if __name__ == '__main__':
    main()
