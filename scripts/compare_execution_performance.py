"""Compare native Present logs; never read pixels or automate a window."""
from pathlib import Path
import argparse
import hashlib
import json
import statistics

ROOT = Path(__file__).resolve().parents[1]
WINDOWS = [(30,45), (45,60), (60,75), (60,90)]


def bound(path):
    return dict(file=str(path.resolve()), sha256=hashlib.sha256(path.read_bytes()).hexdigest())


def summarize(path):
    record = json.loads(path.read_text())
    report = record['report']
    state = report.get('process', report)
    if record['host_return_code'] or record['timed_out'] or report.get('error') or not state['diagnostic_stop']:
        raise ValueError('Incomplete frame-limited run: ' + str(path))
    command = record['command']
    if any(flag in command for flag in ['--profile','--snapshot']):
        raise ValueError('Comparison requires sampling and capture disabled')
    graphics = state['direct3d9']
    frames = graphics['recent_presentations']
    if graphics['frames_captured'] or frames[0][0] != 1:
        raise ValueError('A complete uncaptured Present history is required')
    hz = graphics['presentation_clock_hz']
    origin = frames[0][1]
    rates = []
    for lower, upper in WINDOWS:
        selected = [frame for frame in frames if lower <= (frame[1]-origin)/hz <= upper]
        if len(selected)<2 or (selected[-1][1]-selected[0][1])/hz < .8*(upper-lower):
            rates.append(dict(seconds_after_first_present=[lower,upper], complete=False))
            continue
        deltas = [(b[1]-a[1])*1000/hz for a,b in zip(selected,selected[1:])]
        rates.append(dict(seconds_after_first_present=[lower,upper], complete=True,
            fps=(selected[-1][0]-selected[0][0])*hz/(selected[-1][1]-selected[0][1]),
            frame_ms_median=statistics.median(deltas),
            frame_ms_p95=statistics.quantiles(deltas,n=20,method='inclusive')[18],
            foreground_samples=sum(bool(frame[2]) for frame in selected),
            minimized_samples=sum(bool(frame[3]) for frame in selected),
            samples=len(selected)))
    options = {flag:command[command.index(flag)+1] for flag in ['--root','--budget','--frames']}
    return dict(output_sha256=record['output_sha256'], evidence=bound(path),
        input_sha256=state['input_sha256'], options=options,
        elapsed_seconds=record['elapsed_seconds'], presents=graphics['presents'],
        draw_calls=graphics['draw_calls'], guest_instructions=state['native_instructions'],
        frames_captured=0, intervals=rates)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--before',type=Path,required=True)
    parser.add_argument('--after',type=Path,action='append',required=True)
    parser.add_argument('--output',type=Path,default=ROOT/'logs/execution-performance.json')
    args = parser.parse_args()
    before = summarize(args.before)
    after_runs = [summarize(path) for path in args.after]
    after = after_runs[0]
    for run in after_runs:
        if before['input_sha256']!=run['input_sha256'] or before['options']!=run['options']:
            raise ValueError('The image, resource directory, budget and frame cap must match')
        if run['output_sha256']!=after['output_sha256']:
            raise ValueError('Repeat runs must use the same optimized executable')
    comparisons = []
    for index,old in enumerate(before['intervals']):
        new = [run['intervals'][index] for run in after_runs]
        if old['complete'] and all(item['complete'] for item in new):
            median_fps=statistics.median(item['fps'] for item in new)
            comparisons.append(dict(seconds_after_first_present=old['seconds_after_first_present'],
                before_fps=old['fps'], after_fps=median_fps,
                after_fps_runs=[item['fps'] for item in new], ratio=median_fps/old['fps'],
                minimized_samples=old['minimized_samples']+sum(item['minimized_samples'] for item in new)))
    if not comparisons:
        raise ValueError('No complete matching wall-time interval')
    proof = dict(schema='wa2.execution-performance.v1', output_sha256=after['output_sha256'],
        before=before, after=after, after_repeats=after_runs[1:], comparisons=comparisons,
        sampling_enabled=False, screen_access=False, screenshots=0,
        same_frame_count_does_not_imply_same_animated_workload=True,
        gameplay_verified=False, normal_launcher_verified=False,
        sustained_30fps=all(all(fps>=30 for fps in item['after_fps_runs']) and not item['minimized_samples'] for item in comparisons),
        scope='Unprofiled frame-limited diagnostic; intervals start at first native Present, without visual scene identification.')
    args.output.write_text(json.dumps(proof,indent=2)+'\n')
    print(json.dumps(dict(report=str(args.output),comparisons=comparisons,
        sustained_30fps=proof['sustained_30fps']),indent=2))


if __name__=='__main__':
    main()
