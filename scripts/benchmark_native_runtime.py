"""Time the game's own Present log for a fixed wall interval; no UI access.

Only the child launched here is stopped at the requested measurement limit.
The stop is deliberately reported as diagnostic termination, not normal exit.
"""
from pathlib import Path
import argparse
import hashlib
import json
import statistics
import shutil
import subprocess
import time
import uuid
import os
from process_metrics import cpu_seconds,memory_counters,qpc

ROOT=Path(__file__).resolve().parents[1]
WINDOWS=[(30,45),(45,60),(60,75),(60,90)]


def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def read_report(path):
    try:return json.loads(path.read_text())
    except (FileNotFoundError,json.JSONDecodeError):return None


def presentation_intervals(ordered,hz,origin_frame=1):
    intervals=[]
    origins=[frame[1] for frame in ordered if frame[0]==origin_frame]
    if origins and hz:
        origin=origins[0]
        for lower,upper in WINDOWS:
            selected=[frame for frame in ordered if lower<=(frame[1]-origin)/hz<=upper]
            complete=len(selected)>=2 and (selected[-1][1]-selected[0][1])/hz>=.9*(upper-lower)
            left=right=None
            if origin_frame!=1:
                before=[frame for frame in ordered if (frame[1]-origin)/hz<=lower]
                through=[frame for frame in ordered if (frame[1]-origin)/hz<upper]
                left=before[-1] if before else None;right=through[-1] if through else None
                complete=bool(left and right and (ordered[-1][1]-origin)/hz>=upper)
            item={('seconds_after_first_present' if origin_frame==1 else 'seconds_after_frame_'+str(origin_frame)):[lower,upper],'complete':complete}
            if complete:
                deltas=[(b[1]-a[1])*1000/hz for a,b in zip(selected,selected[1:]) if b[0]==a[0]+1]
                item.update(fps=(right[0]-left[0])/(upper-lower) if origin_frame!=1 else (selected[-1][0]-selected[0][0])*hz/(selected[-1][1]-selected[0][1]),
                    foreground_samples=sum(bool(frame[2]) for frame in selected),
                    minimized_samples=sum(bool(frame[3]) for frame in selected),samples=len(selected),
                    frame_ms_median=statistics.median(deltas) if deltas else None,
                    frame_ms_p95=statistics.quantiles(deltas,n=20,method='inclusive')[18] if len(deltas)>1 else None)
            intervals.append(item)
    return intervals


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--program',type=Path,required=True)
    parser.add_argument('--root',type=Path,default=ROOT/'res')
    parser.add_argument('--seconds',type=float,default=125)
    parser.add_argument('--evidence',type=Path,required=True)
    parser.add_argument('--no-sampler-cache',action='store_true')
    parser.add_argument('--ordinary',action='store_true',help='Use embedded-image ordinary launch options, without a diagnostic frame ceiling')
    parser.add_argument('--background',action='store_true',help='Request SW_SHOWNOACTIVATE at process creation; do not change focus after launch')
    parser.add_argument('--native-sleep',action='store_true',help='Compare native Win32 short Sleep calls with precise timer waits')
    args=parser.parse_args()
    if not 10<=args.seconds<=300:raise ValueError('Measurement duration must be 10..300 seconds')
    program=args.program.resolve();evidence=args.evidence.resolve()
    for protected in (program,(ROOT/'WA2_chs.exe').resolve()):
        if evidence==protected or (evidence.exists() and evidence.samefile(protected)):
            raise ValueError('Evidence must not overwrite an executable')
    case=ROOT/'build/native-backend'/('measure-'+uuid.uuid4().hex);case.mkdir(parents=True)
    archive=case/'program.exe';shutil.copy2(program,archive)
    if digest(archive)!=digest(program):raise ValueError('Executable changed while archiving measurement input')
    def snapshot_profile(label):
        source=args.root.resolve()/'Documents/Leaf/WHITE ALBUM2';target=case/label;target.mkdir()
        hashes={}
        for name in ('D3D.ini','SYSTEM.ini','Sys.sav'):
            if (source/name).is_file():
                shutil.copy2(source/name,target/name);hashes[name]=digest(target/name)
        return dict(directory=str(target),sha256=hashes)
    profile_before=snapshot_profile('profile-before')
    report_path=case/'runtime.json';console_path=case/'console.log'
    # A remote frame ceiling enables the existing diagnostic mutex isolation
    # and fail-on-modal-dialog behavior. It cannot end a 300-second sample.
    command=[str(program)]
    if not args.ordinary:command.append(str(ROOT/'WA2_chs.exe'))
    command+=['--root',str(args.root.resolve())]
    if not args.ordinary:command+=['--budget','1000000000000','--frames','1000000000']
    command+=['--report',str(report_path)]
    frames={};buffers={};latest=None;stopped=False;cpu_samples=[];scheduler_samples=[];performance_samples=[];allocation_samples=[];last_memory=None
    started=time.perf_counter()
    environment=os.environ.copy();overrides={'WA2_NATIVE_SLEEP':'1' if args.native_sleep else '0',
        'WA2_NATIVE_EXITS':'0','WA2_NO_SAMPLER_CACHE':'1' if args.no_sampler_cache else '0'}
    environment.update(overrides)
    startup=None
    if args.background:
        startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow=4 # SW_SHOWNOACTIVATE; leave the current foreground alone.
    with console_path.open('wb') as console:
        process=subprocess.Popen(command,cwd=ROOT,stdout=console,stderr=subprocess.STDOUT,env=environment,startupinfo=startup)
        try:
            while True:
                used=cpu_seconds(process._handle)
                if used is not None:cpu_samples.append([qpc(),used])
                if process.poll() is None:last_memory=memory_counters(process._handle)
                report=read_report(report_path)
                if report:
                    latest=report;state=report.get('process',report)
                    graphics=state.get('direct3d9',{})
                    clock=graphics.get('reported_counter')
                    if clock and (not performance_samples or performance_samples[-1]['qpc']!=clock):
                        performance_samples.append(dict(qpc=clock,presents=graphics.get('presents',0),draws=graphics.get('draw_calls',0),
                            state_applies=graphics.get('state_applies'),state_cache_hits=graphics.get('state_cache_hits'),
                            texture_only_applies=graphics.get('texture_only_applies')))
                        sound=state.get('directsound',{})
                        allocation_samples.append(dict(qpc=clock,host_memory=last_memory,
                            guest=state.get('guest_allocations'),graphics=state.get('sdl_resource_lifetimes'),
                            sound={key:sound.get(key) for key in ('live_objects','active_traces','retired_traces')}))
                    timing=state.get('threads',{}).get('main_sleep')
                    if timing:scheduler_samples.append([qpc(),timing])
                    if graphics.get('frames_captured',0):raise ValueError('Capture was unexpectedly enabled')
                    for frame in graphics.get('recent_presentations',[]):frames[frame[0]]=frame
                    for buffer in state.get('directsound',{}).get('buffer_timing',[]):
                        target=buffers.setdefault(buffer.get('lifetime',buffer['object']),dict(info=buffer,events={}))
                        target['info']=buffer
                        for event in buffer['recent']:
                            key=tuple(event[key] for key in ('milliseconds','operation','thread','offset','bytes','sample_hash'))
                            target['events'][key]=event
                if process.poll() is not None:break
                if time.perf_counter()-started>=args.seconds:
                    process.kill();stopped=True;break
                time.sleep(.25)
        finally:
            if process.poll() is None:process.kill()
            process.wait(timeout=15)
    if latest is None:raise RuntimeError('Child did not produce a runtime report')
    valid_report=case/'runtime-last-valid.json';valid_report.write_text(json.dumps(latest)+'\n')
    state=latest.get('process',latest)
    graphics=state.get('direct3d9',{})
    ordered=sorted(frames.values())
    hz=graphics.get('presentation_clock_hz',0)
    intervals=presentation_intervals(ordered,hz)
    title_intervals=presentation_intervals(ordered,hz,30)
    audio=[]
    for target in buffers.values():
        info=target['info'];bps=info['bytes_per_second'];size=info['bytes']
        if not bps or not size:continue
        writes=sorted((event for event in target['events'].values() if event['operation']=='unlock' and event['thread']>1
            and size/4<=event['bytes']<=size/2),key=lambda event:event['milliseconds'])
        if len(writes)<2:continue
        audio.append(dict(buffer_bytes=size,bytes_per_second=bps,refills=len(writes),
            max_decode_ms=max(event['locked_ms'] for event in writes),
            missed_decode_deadlines=sum(event['locked_ms']>=1000*event['bytes']/bps for event in writes),
            writes_into_playing_half=sum((event['play_cursor']-event['offset'])%size<event['bytes'] for event in writes)))
    total=state.get('native_instructions',0);fast=state.get('static_batches',{}).get('guest_instructions',0)
    blocks=state.get('static_blocks',{}).get('guest_instructions',0)
    record=dict(schema='wa2.native-wall-benchmark.v1',output_sha256=digest(program),
        launch_mode='ordinary' if args.ordinary else 'diagnostic',
        requested_window_state='background' if args.background else 'default',
        profile_before=profile_before,profile_after=snapshot_profile('profile-after'),
        environment_overrides=overrides,
        program_archive=dict(file=str(archive),sha256=digest(archive)),
        input_sha256=state.get('input_sha256'),command=command,pid=process.pid,root=str(args.root.resolve()),
        requested_seconds=args.seconds,elapsed_seconds=round(time.perf_counter()-started,3),
        timed_measurement_stop=stopped,host_return_code=process.returncode,error=latest.get('error'),
        report_file=str(valid_report),report_sha256=digest(valid_report),source_report_file=str(report_path),console_file=str(console_path),
        program_frames=graphics.get('presents',0),collected_presentations=ordered,
        intervals=intervals,title_intervals=title_intervals,title_origin_frame=30,
        movie_completed_naturally=1 in state.get('directshow',{}).get('event_codes',[]),
        performance_samples=performance_samples,allocation_samples=allocation_samples,audio=audio,native_execution=state.get('native_execution'),
        collected_audio=[dict(info=target['info'],events=list(target['events'].values())) for target in buffers.values()],
        execution_mix=dict(total_guest_instructions=total,fast_batch_instructions=fast,
            cpp_block_instructions=blocks,single_instruction_fallback=max(total-fast-blocks,0)),
        frames_captured=0,screen_access=False,profile_enabled=False,normal_exit_verified=False,
        process_cpu_seconds=cpu_seconds(process._handle),process_memory=last_memory,cpu_samples=cpu_samples,scheduler_samples=scheduler_samples,
        gameplay_verified=False,scope='Fixed wall-time diagnostic, isolated single-instance mutex; only child-owned JSON counters, no visual scene verification.')
    evidence.parent.mkdir(parents=True,exist_ok=True);evidence.write_text(json.dumps(record,indent=2)+'\n')
    print(json.dumps({key:record[key] for key in ('output_sha256','elapsed_seconds','timed_measurement_stop','error','movie_completed_naturally',
        'program_frames','intervals','title_intervals','audio','native_execution','execution_mix')},indent=2))
    if latest.get('error') or not stopped or not record['movie_completed_naturally'] or not ordered or ordered[0][0]!=1 or not title_intervals or not all(item['complete'] for item in title_intervals):return 1
    return 0


if __name__=='__main__':raise SystemExit(main())
