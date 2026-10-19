"""Observe only a child game's own timing reports; never inspect its window."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import time
import uuid

ROOT=Path(__file__).resolve().parents[1]
def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--program',type=Path,required=True)
    parser.add_argument('--seconds',type=float,default=20)
    parser.add_argument('--evidence',type=Path,required=True)
    parser.add_argument('--frame-trace',action='store_true')
    parser.add_argument('--until-logo-complete',action='store_true')
    args=parser.parse_args()
    if args.until_logo_complete and not args.frame_trace:raise ValueError('Logo completion requires numerical frame tracing')
    if not 15<=args.seconds<=120:raise ValueError('Observation must be 15..120 seconds')
    case=ROOT/'build/logo-fade'/uuid.uuid4().hex;case.mkdir(parents=True)
    program=case/'program.exe';shutil.copy2(args.program.resolve(),program)
    value=digest(program)
    if value!=digest(args.program):raise ValueError('Program changed during archiving')
    runtime=case/'runtime.json';console=case/'console.log'
    command=[str(program),'--root',str(ROOT/'res'),'--report',str(runtime)]
    startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=4
    frames={};writes={};stages={};opacity={};loads={};latencies={};latest=None;snapshots=[];last_counter=None
    environment=os.environ.copy()
    if args.frame_trace:environment['WA2_FRAME_TRACE']='1'
    else:environment.pop('WA2_FRAME_TRACE',None)
    started=time.monotonic();alive=False;logo_complete=False
    with console.open('wb') as output:
        process=subprocess.Popen(command,cwd=ROOT,stdout=output,stderr=subprocess.STDOUT,startupinfo=startup,env=environment)
        try:
            while time.monotonic()-started<args.seconds and process.poll() is None:
                try:state=json.loads(runtime.read_text(encoding='utf-8-sig'))
                except (OSError,ValueError):time.sleep(.05);continue
                latest=state
                if state.get('error'):break
                state=state.get('process',state);graphics=state.get('direct3d9',{})
                if graphics.get('frames_captured'):raise ValueError('Unexpected capture')
                for frame in graphics.get('recent_presentations',[]):frames[frame[0]]=frame
                for row in state.get('runtime_report_timing',{}).get('recent',[]):writes[row[0]]=row
                for row in state.get('frame_timing',{}).get('recent',[]):stages[row[0]]=row
                for row in state.get('frame_timing',{}).get('opacity',[]):opacity[(row[0],row[1],row[2])]=row
                for row in state.get('resource_load_trace',{}).get('recent',[]):loads[row[0]]=row
                for row in state.get('api_latency',{}).get('events',[]):latencies[(row['counter'],row['kind'])]=row
                counter=graphics.get('reported_counter')
                if counter and counter!=last_counter:
                    snapshots.append(dict(counter=counter,presents=graphics.get('presents'),
                        resources=state.get('native_resources'),graphics=state.get('resource_graphics'),
                        report_timing=state.get('runtime_report_timing'),
                        movie={key:state.get('directshow',{}).get(key) for key in ['files_rendered','stops','live_guest_interfaces','event_codes','call_sites']}))
                    last_counter=counter
                if args.until_logo_complete:
                    first_fade=next((v for v in sorted(opacity.values()) if 0<v[5]<255),None)
                    if first_fade and any(v[2]==first_fade[2] and v[5]==255 and v[0]>first_fade[0] for v in opacity.values()):
                        logo_complete=True;break
                time.sleep(.05)
            alive=process.poll() is None
        finally:
            if process.poll() is None:process.terminate()
            process.wait(timeout=10)
    if latest is None:raise RuntimeError('Missing runtime report')
    state=latest.get('process',latest);graphics=state.get('direct3d9',{});hz=graphics.get('presentation_clock_hz',0)
    ordered=sorted(frames.values());timings=sorted(writes.values());slow=[]
    for a,b in zip(ordered,ordered[1:]):
        if b[0]!=a[0]+1:continue
        milliseconds=(b[1]-a[1])*1000/hz
        if milliseconds<8:continue
        overlaps=[dict(build_ms=row[1]/1e6,write_ms=row[2]/1e6) for row in timings
            if a[1]<=row[0]<=b[1]]
        slow.append(dict(frame=b[0],milliseconds=milliseconds,foreground=bool(b[2]),minimized=bool(b[3]),report_work=overlaps))
    result=dict(schema='wa2.logo-fade-timing.v1',output_sha256=value,program=str(program),command=command,
        requested_seconds=args.seconds,elapsed_seconds=time.monotonic()-started,alive_at_test_stop=alive,
        ended_by_test_timer=alive and not logo_complete,stopped_after_logo_completed=logo_complete,error=latest.get('error'),runtime=dict(file=str(runtime),sha256=digest(runtime)),
        console=dict(file=str(console),sha256=digest(console)),presentation_clock_hz=hz,frames=ordered,
        report_writes=timings,slow_intervals=slow,snapshots=snapshots,
        frame_trace=args.frame_trace,frame_stages=sorted(stages.values()),opacity=sorted(opacity.values()),resource_loads=sorted(loads.values()),api_latencies=sorted(latencies.values(),key=lambda v:v['counter']),
        screen_access=False,automated_input=False,normal_exit_verified=False,visual_smoothness_verified=False)
    args.evidence.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(dict(output_sha256=value,frames=len(ordered),error=result['error'],
        report_timing=state.get('runtime_report_timing'),slow_intervals=slow[:30]),indent=2))
    if not alive or result['error']:raise RuntimeError('Owned game failed during timing observation')

if __name__=='__main__':main()
