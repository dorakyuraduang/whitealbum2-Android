"""Bind original PID-only ETW and native Present counters to matched assets.

Both use the fixed 30th Present as the time origin. This does not identify a
scene from pixels, check gameplay, or turn diagnostic termination into an exit.
"""
from pathlib import Path
import argparse
import csv
import hashlib
import json
import os
from benchmark_native_runtime import presentation_intervals, WINDOWS

ROOT=Path(__file__).resolve().parents[1]


def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def bound(path):return dict(file=str(Path(path).resolve()),sha256=digest(path))
def window_state(foreground_fraction):
    return 'foreground' if foreground_fraction>=.95 else 'background' if foreground_fraction<=.05 else 'mixed'


def native(path):
    record=json.loads(path.read_text())
    if record['schema']!='wa2.native-wall-benchmark.v1':raise ValueError('Unknown native benchmark')
    if record['error'] or record['screen_access'] or record['frames_captured'] or record['profile_enabled']:
        raise ValueError('Native run failed or enabled capture/instruction profiling')
    if not record.get('movie_completed_naturally'):raise ValueError('Opening movie did not complete naturally')
    if not record.get('audio') or any(item['missed_decode_deadlines'] or item['writes_into_playing_half'] for item in record['audio']):
        raise ValueError('Missing streamed audio timing, missed decode deadline, or write into the playing half')
    if not record['timed_measurement_stop'] or record['elapsed_seconds']<record['requested_seconds']:
        raise ValueError('Incomplete native measurement')
    archive=record['program_archive']
    if digest(archive['file'])!=record['output_sha256'] or archive['sha256']!=record['output_sha256']:
        raise ValueError('Native archive changed')
    if digest(record['report_file'])!=record['report_sha256']:raise ValueError('Native report changed')
    runtime=json.loads(Path(record['report_file']).read_text());state=runtime.get('process',runtime)
    if runtime.get('error') or state.get('diagnostic_stop') or state['direct3d9']['frames_captured']:
        raise ValueError('Native runtime did not remain running without capture')
    if state.get('instruction_fallback',{}).get('enabled') or 'sampled_exits' in state.get('native_execution',{}):
        raise ValueError('Native run enabled instruction/exit sampling')
    frames=record['collected_presentations']
    if not frames or frames[0][0]!=1 or any(b[0]<=a[0] or b[1]<=a[1] for a,b in zip(frames,frames[1:])):
        raise ValueError('Invalid native presentation origin/order')
    intervals=presentation_intervals(frames,state['direct3d9']['presentation_clock_hz'],30)
    if intervals!=record['title_intervals'] or not all(i['complete'] for i in intervals):
        raise ValueError('Incomplete/mismatched native intervals')
    if state['input_sha256']!=record['input_sha256']:raise ValueError('Native input mismatch')
    return record,dict(evidence=bound(path),runtime_report=bound(record['report_file']),
        output_sha256=record['output_sha256'],launch_mode=record.get('launch_mode','diagnostic'),
        root=record['root'],intervals=intervals,audio=record['audio'],
        process_cpu_seconds=record['process_cpu_seconds'],cpu_features=state.get('wa2_cpu_features'))


def comparison(original_path,native_paths,ordinary_path=None):
    original=json.loads(original_path.read_text())
    if original['schema']!='wa2.original-etw-benchmark.v1' or original['error'] or original['presentmon_return_code']:
        raise ValueError('Original benchmark failed')
    if original['screen_access'] or original['input_tracking'] or original['automated_input']:
        raise ValueError('Original benchmark used disallowed observation/input')
    if not original['timed_measurement_stop'] or original['elapsed_seconds']<original['requested_seconds']:
        raise ValueError('Incomplete original interval')
    if digest(original['program_file'])!=original['input_sha256'] or digest(original['csv_file'])!=original['csv_sha256']:
        raise ValueError('Original executable or ETW evidence changed')
    if not original['profile_before'] or original['profile_before']!=original['profile_after']:
        raise ValueError('Original settings changed or were not recorded')
    with open(original['csv_file'],encoding='utf-8-sig',newline='') as stream:rows=list(csv.DictReader(stream))
    if len(rows)!=original['present_rows'] or len(rows)<30:raise ValueError('Missing original Present events')
    chains={row['SwapChainAddress'] for row in rows}
    if len(chains)!=1 or any(int(row['ProcessID'])!=original['pid'] or row['Runtime']!='D3D9' or row['Dropped']!='0' for row in rows):
        raise ValueError('ETW contains another PID/chain/runtime or dropped events')
    times=[float(row['TimeInSeconds']) for row in rows]
    if times!=sorted(set(times)):raise ValueError('Invalid original presentation order')
    origin=times[29];baseline=[]
    for lower,upper in WINDOWS:
        if times[-1]<origin+upper:raise ValueError('Original sustained interval incomplete')
        count=sum(origin+lower<=t<origin+upper for t in times)
        baseline.append(dict(seconds_after_frame_30=[lower,upper],fps=count/(upper-lower)))
    output=digest(ROOT/'out/WA2_SDL2_x64.exe');input_hash=digest(ROOT/'WA2_chs.exe')
    if original['input_sha256']!=input_hash:raise ValueError('Original input changed')
    original_foreground=sum(bool(s[1]) for s in original['samples'])/len(original['samples'])
    original_state=window_state(original_foreground)
    original_window_observed=sum(s[2] is not None for s in original['samples'])/len(original['samples'])
    original_minimized=sum(bool(s[2]) for s in original['samples'])
    feature_samples=[value for clock,value in original.get('crt_feature_samples',[])
        if clock>=original['game_started_qpc']+30*original['qpc_hz']]
    if not feature_samples or not feature_samples[-1] or any(value!=feature_samples[-1] for value in feature_samples):
        raise ValueError('Original CRT CPU features were not observed consistently after startup')
    original_features=feature_samples[-1]
    measured=[];comparisons=[]
    for path in native_paths:
        record,summary=native(path)
        if summary['cpu_features']!=original_features:raise ValueError('Native and original CRT CPU features differ')
        if record['output_sha256']!=output or record['input_sha256']!=input_hash:raise ValueError('Native measurement is from another build/input')
        if record['requested_seconds']!=original['requested_seconds']:raise ValueError('Measurement durations differ')
        root=Path(record['root'])
        profile=root/'Documents/Leaf/WHITE ALBUM2'
        for label in ('profile_before','profile_after'):
            snapshot=record.get(label)
            if not snapshot or snapshot['sha256']!=original['profile_before']:
                raise ValueError('Matched native settings were not recorded or changed during measurement')
            if any(digest(Path(snapshot['directory'])/name)!=value for name,value in snapshot['sha256'].items()):
                raise ValueError('Native profile snapshot changed')
        if any(digest(profile/name)!=value for name,value in original['profile_before'].items()):
            raise ValueError('Native and original profile settings differ')
        if not all(os.path.samefile(Path(original['root'])/name,root/name) for name in original['linked_resources']):
            raise ValueError('Original/native resource files differ')
        measured.append(summary)
        for old,new in zip(baseline,summary['intervals']):
            foreground=not new['minimized_samples'] and new['foreground_samples']>=.95*new['samples']
            native_state=window_state(new['foreground_samples']/new['samples'])
            comparisons.append(dict(native_evidence=str(path),seconds_after_frame_30=old['seconds_after_frame_30'],
                original_fps=old['fps'],native_fps=new['fps'],fps_ratio=new['fps']/old['fps'],
                native_frame_time_overhead_percent=100*(old['fps']/new['fps']-1),foreground=foreground,
                window_state=native_state,matched_window_state=native_state==original_state and native_state!='mixed',
                native_minimized_samples=new['minimized_samples']))
    ordinary=None
    if ordinary_path:
        record,ordinary=native(ordinary_path)
        if ordinary['cpu_features']!=original_features:raise ValueError('Ordinary launch CRT CPU features differ')
        if ordinary['output_sha256']!=output or ordinary['launch_mode']!='ordinary':raise ValueError('Ordinary test used another build/mode')
    result=dict(schema='wa2.original-native-performance.v1',input_sha256=input_hash,output_sha256=output,
        original=dict(evidence=bound(original_path),etw=bound(original['csv_file']),intervals=baseline,
            profile=original['profile_before'],foreground_fraction=original_foreground,window_state=original_state,
            window_observed_fraction=original_window_observed,minimized_samples=original_minimized,cpu_features=original_features),
        native=measured,ordinary=ordinary,comparisons=comparisons,
        matched_resources_and_profile=True,near_original_90_percent=original_foreground>=.95 and all(c['fps_ratio']>=.9 and c['foreground'] for c in comparisons),
        near_original_90_percent_in_matched_state=original_window_observed>=.95 and not original_minimized and
            all(c['fps_ratio']>=.9 and c['matched_window_state'] and not c['native_minimized_samples'] for c in comparisons),
        screen_access=False,automated_input=False,gameplay_verified=False,normal_exit_verified=False,
        limits='Matched assets/profile, fixed wall intervals after Present 30. Present calls/events do not prove identical scene pixels or complete gameplay. Children were intentionally stopped after measurement.')
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--original',type=Path,required=True)
    parser.add_argument('--native',type=Path,action='append',required=True)
    parser.add_argument('--ordinary',type=Path)
    parser.add_argument('--evidence',type=Path,default=ROOT/'logs/original-native-performance.json')
    args=parser.parse_args();result=comparison(args.original,args.native,args.ordinary)
    args.evidence.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(dict(comparisons=result['comparisons'],near_original_90_percent=result['near_original_90_percent'],
        near_original_90_percent_in_matched_state=result['near_original_90_percent_in_matched_state']),indent=2))


if __name__=='__main__':main()
