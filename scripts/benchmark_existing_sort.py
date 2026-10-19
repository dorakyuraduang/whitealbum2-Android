"""Exercise existing generated code; never open or disassemble the source EXE."""
import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--program', type=Path, default=ROOT/'build/wa2-win64/wa2_port/wa2_existing_sort_tests.exe')
    parser.add_argument('--report', type=Path, default=ROOT/'logs/existing-sort-verified.json')
    parser.add_argument('--baseline', action='store_true', help='Use the previous whole-object search path in the same test executable')
    args = parser.parse_args()
    program = args.program.resolve()
    report = args.report.resolve()
    if not program.is_relative_to(ROOT) or not report.is_relative_to(ROOT):
        raise ValueError('Test executable and output must be inside the workspace')
    if report == program:
        raise ValueError('Report cannot overwrite the test executable')
    source = ROOT/'port/native_backend_data.inc'
    match = re.search(r'native_signature_1090=\{([^}]+)\}', source.read_text())
    if match is None:
        raise ValueError('Missing existing generated code page')
    page = bytes(map(int, match.group(1).split(',')))
    if len(page) != 4096:
        raise ValueError('Invalid existing code-page size')
    directory = ROOT/'build/existing-code-tests'
    directory.mkdir(parents=True, exist_ok=True)
    fixture = directory/'sort-page.bin'
    fixture.write_bytes(page)
    report.parent.mkdir(parents=True, exist_ok=True)
    log = report.with_suffix('.log')
    fingerprint = digest(program)
    sources = {str(path.relative_to(ROOT)): digest(path) for path in (
        ROOT/'port/fast_paths.cpp', ROOT/'port/sprite_sort.cpp', ROOT/'port/sprite_sort.hpp', ROOT/'tests/existing_sort.cpp',
        ROOT/'tests/sort_objects.hpp', ROOT/'scripts/benchmark_existing_sort.py')}
    with log.open('wb') as console:
        command = [str(program), str(fixture), str(report)]
        if args.baseline:
            command.append('--baseline')
        subprocess.run(command, cwd=ROOT,
                       stdout=console, stderr=subprocess.STDOUT, timeout=180, check=True)
    if digest(program) != fingerprint:
        raise ValueError('Test executable changed during measurement')
    proof = json.loads(report.read_text())
    if proof['cases'] < 603 or not proof['exact_cpu_flags_fpu_memory_budget_faults']:
        raise ValueError('Generated-code comparisons did not complete')
    if proof['original_executable_opened'] or proof['screen_access'] or proof['automated_input']:
        raise ValueError('Unexpected test scope')
    if proof['partial_search_enabled'] != (not args.baseline):
        raise ValueError('Unexpected search backend')
    proof.update(program_sha256=fingerprint, fixture_sha256=digest(fixture),
                 generated_signature_sha256=digest(source), source_sha256=sources,
                 console_sha256=digest(log), published_runtime_changed=False)
    report.write_text(json.dumps(proof, indent=2)+'\n')
    print(json.dumps({key: proof[key] for key in ('cases', 'benchmarks', 'whole_game_performance_measured')}, indent=2))


if __name__ == '__main__':
    main()
