"""Bind native-backend wall-time measurements to their own Present logs."""
from pathlib import Path
import argparse
import hashlib
import json
import statistics
from benchmark_native_runtime import presentation_intervals

ROOT=Path(__file__).resolve().parents[1]


def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def summarize(path,program_override=None):
    path=path.resolve();record=json.loads(path.read_text())
    if record['schema']!='wa2.native-wall-benchmark.v1':raise ValueError('Unknown measurement format')
    if record['screen_access'] or record['profile_enabled'] or record['frames_captured'] or record['error']:
        raise ValueError('Measurement enabled capture/profile or encountered a runtime error')
    if not record['timed_measurement_stop'] or record['elapsed_seconds']<record['requested_seconds']:
        raise ValueError('Child exited before the timed measurement ended')
    program=Path(program_override or record.get('program_archive',{}).get('file',record['command'][0]));report_file=Path(record['report_file'])
    if digest(program)!=record['output_sha256'] or digest(report_file)!=record['report_sha256']:
        raise ValueError('Executable or runtime evidence changed')
    runtime=json.loads(report_file.read_text());state=runtime.get('process',runtime)
    graphics=state['direct3d9']
    if runtime.get('error') or graphics['frames_captured'] or state.get('diagnostic_stop'):
        raise ValueError('Runtime report contains an error, capture, or frame-limited exit')
    if graphics['presents']!=record['program_frames'] or state['input_sha256']!=record['input_sha256']:
        raise ValueError('Runtime totals no longer match the measurement')
    frames=record['collected_presentations']
    if not frames or frames[0][0]!=1 or frames[-1][0]>graphics['presents']:
        raise ValueError('Missing presentation origin or invalid frame identity')
    if any(b[0]<=a[0] or b[1]<=a[1] for a,b in zip(frames,frames[1:])):
        raise ValueError('Non-monotonic presentation counters')
    intervals=presentation_intervals(frames,graphics['presentation_clock_hz'])
    if intervals!=record['intervals'] or not all(item['complete'] for item in intervals):
        raise ValueError('Incomplete or mismatched sustained intervals')
    return dict(output_sha256=record['output_sha256'],input_sha256=record['input_sha256'],
        evidence=dict(file=str(path),sha256=digest(path)),
        runtime_report=dict(file=str(report_file),sha256=record['report_sha256']),
        options={key:value for key,value in zip(record['command'][2::2],record['command'][3::2]) if key!='--report'},
        requested_seconds=record['requested_seconds'],elapsed_seconds=record['elapsed_seconds'],
        presents=record['program_frames'],intervals=intervals,native_execution=record['native_execution'],
        execution_mix=record['execution_mix'],audio=record['audio'])


def comparisons(before,after):
    result=[]
    for index,baseline in enumerate(before['intervals']):
        rates=[item['intervals'][index]['fps'] for item in after]
        middle=statistics.median(rates)
        result.append(dict(seconds_after_first_present=baseline['seconds_after_first_present'],
            before_fps=baseline['fps'],after_fps=rates,after_median_fps=middle,
            median_speedup=middle/baseline['fps'],sustained_30fps=all(rate>=30 for rate in rates)))
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--before',type=Path,required=True)
    parser.add_argument('--after',type=Path,action='append',required=True)
    parser.add_argument('--excluded-minimized',type=Path,action='append',default=[])
    parser.add_argument('--archive-program',type=Path,action='append',default=[])
    parser.add_argument('--evidence',type=Path,default=ROOT/'logs/native-performance.json')
    args=parser.parse_args()
    archives={digest(path):str(path.resolve()) for path in args.archive_program}
    def measured(path):
        identity=json.loads(path.read_text())['output_sha256'];return summarize(path,archives.get(identity))
    before=measured(args.before);after=[measured(path) for path in args.after]
    output=digest(ROOT/'out/WA2_SDL2_x64.exe')
    for run in after:
        if run['output_sha256']!=output:raise ValueError('Candidate measurements are from another build')
    for run in [before,*after]:
        if run['input_sha256']!=digest(ROOT/'WA2_chs.exe') or run['options']!=before['options'] or run['requested_seconds']!=before['requested_seconds']:
            raise ValueError('Measurements used different input, options or duration')
    rates=comparisons(before,after)
    excluded=[]
    for path in args.excluded_minimized:
        run=measured(path)
        if not any(item['minimized_samples'] for item in run['intervals']):raise ValueError('Excluded run was not minimized')
        excluded.append(dict(run=run,reason='Window minimized during sustained intervals; excluded from the FPS comparison.'))
    proof=dict(schema='wa2.native-performance.v1',output_sha256=output,before=before,after=after,
        program_archives=archives,
        excluded_diagnostics=excluded,
        comparisons=rates,sustained_30fps=all(item['sustained_30fps'] for item in rates),
        screen_access=False,profile_enabled=False,normal_exit_verified=False,gameplay_verified=False,
        limits='Only child-owned Present and audio logs. Foreground state is recorded, not controlled; scene pixels and full gameplay were not checked. Each run was intentionally terminated after the timed diagnostic.')
    args.evidence.write_text(json.dumps(proof,indent=2)+'\n')
    print(json.dumps(dict(output_sha256=output,comparisons=rates,sustained_30fps=proof['sustained_30fps']),indent=2))


if __name__=='__main__':main()
