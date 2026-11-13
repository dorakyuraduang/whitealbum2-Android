"""Observe text progression or compare paths in an already running phone game.

The user selects the scene and keeps it foreground. Shell-protected registered
broadcasts switch only the candidate path and read actual frame HUD
counters. No activity launch, input event, screenshot or resource access.
"""
import argparse
import json
from pathlib import Path
import re
import statistics
import subprocess
import time
from build_pak_tool import low_priority

ROOT=Path(__file__).resolve().parents[1]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial',required=True)
    parser.add_argument('--samples',type=int,choices=range(4,13),default=8)
    parser.add_argument('--observe-only',action='store_true',help='Read counters without changing any execution/rendering mode')
    parser.add_argument('--label',default='unspecified',help='User-selected phase, e.g. after-load or after-advance')
    parser.add_argument('--output',type=Path,default=ROOT/'logs/android-text-comparison.json')
    args=parser.parse_args();low_priority()
    adb=ROOT/'tools/android-sdk/platform-tools/adb.exe'
    flags=getattr(subprocess,'CREATE_NO_WINDOW',0)|getattr(subprocess,'BELOW_NORMAL_PRIORITY_CLASS',0)
    release=json.loads((ROOT/'config/release.json').read_text())['name']
    def call(*values):
        return subprocess.check_output([str(adb),'-s',args.serial,*values],creationflags=flags,timeout=20,text=True,encoding='utf-8',errors='replace')
    pid=call('shell','pidof','org.wa2.port').strip()
    if not pid.isdecimal():raise RuntimeError('One existing game process is required; this tool never starts it')
    package=call('shell','dumpsys','package','org.wa2.port')
    if not re.search(r'versionName='+re.escape(release)+r'\s',package):raise RuntimeError('Installed version differs from the current source release')
    uid=re.search(r'^package:org\.wa2\.port uid:(\d+)\s*$',call('shell','cmd','package','list','packages','-U','org.wa2.port'),re.M)
    if not uid:raise RuntimeError('Cannot identify the exact game UID')
    last_response=-1
    def request(mode):
        nonlocal last_response
        if call('shell','pidof','org.wa2.port').strip()!=pid:raise RuntimeError('Game process changed during the comparison')
        # Registered-only delivery cannot start an application process.
        call('shell','am','broadcast','--receiver-registered-only','-p','org.wa2.port','-a','org.wa2.port.PERFORMANCE','--es','mode',mode)
        # Filter by UID/tag before trimming locally. Android's -t can select
        # the last global records before these filters and hide our response.
        text=call('logcat','-d','--uid='+uid[1],'-v','brief','WA2PERF:I','*:S')
        if len(text.encode('utf-8'))>1024*1024:raise RuntimeError('Own performance log exceeds the bounded reader')
        records=[]
        for line in text.splitlines()[-128:]:
            try:record=json.loads(line[line.index('{'):])
            except (ValueError,IndexError):continue
            if record.get('version')==release:records.append(record)
        if not records:raise RuntimeError('No live performance response; game must be open and foreground')
        record=records[-1]
        expected='reference' if mode=='reference_hold' else mode
        if record['uptime_ms']<=last_response or (mode!='snapshot' and record.get('mode')!=expected):
            raise RuntimeError('No fresh performance response from the foreground game')
        last_response=record['uptime_ms'];return record
    if args.observe_only:
        rows=[]
        for _ in range(args.samples):
            row=request('snapshot');work=row.get('text_work',{})
            if not row.get('valid') or not row.get('foreground') or not row.get('focused') or row['sample_age_ms']>1000:
                raise RuntimeError('No fresh foreground frame sample')
            if not work.get('presents') or not work.get('monotonic_ns'):raise RuntimeError('No owner-thread text work sample')
            if rows and work['presents']<=rows[-1]['text_work']['presents']:raise RuntimeError('Text work sample is stale')
            rows.append(row);print('snapshot FPS '+str(round(row['fps'],2)),flush=True);time.sleep(.85)
        first,last=rows[0]['text_work'],rows[-1]['text_work'];frames=last['presents']-first['presents']
        elapsed=last['monotonic_ns']-first['monotonic_ns']
        gauges={'live_objects','retained_packets','retained_bytes','source_fp_control','monotonic_ns'}
        rates={key:(last[key]-value)/frames for key,value in first.items() if key not in gauges and key!='presents'}
        if elapsed<=0 or any(value<0 for value in rates.values()):raise RuntimeError('Counters reset inside one observation')
        report=dict(release=release,pid=int(pid),label=args.label,samples=rows,frames=frames,elapsed_ns=elapsed,
            actual_present_fps=frames*1e9/elapsed,per_frame=rates,gauges={key:last[key] for key in gauges},
            app_launched=False,screen_access=False,game_ui_operated=False,modes_changed=False,measured_on_phone=True,passed=True)
        args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(report,indent=2)+'\n')
        print(json.dumps({k:v for k,v in report.items() if k!='samples'},indent=2));return
    windows=[];failure=None
    try:
        for mode in ('reference','optimized','optimized','reference'):
            first=request(mode);start=first['uptime_ms'];time.sleep(1.1);rows=[]
            for _ in range(args.samples):
                row=request('snapshot')
                if not row.get('valid') or not row.get('foreground') or not row.get('focused') or row.get('mode')!=mode:
                    raise RuntimeError('No comparable foreground frame sample')
                if row['uptime_ms']-row['sample_age_ms']<start+500:raise RuntimeError('Frame sample predates the mode change')
                rows.append(row);time.sleep(.85)
            delta=rows[-1]['candidate_calls']-first['candidate_calls']
            if mode=='optimized' and delta<=0:raise RuntimeError('Candidate path was not admitted in this scene')
            if mode=='reference' and delta>1:raise RuntimeError('Reference window unexpectedly used the candidate path')
            window=dict(mode=mode,samples=rows,median_fps=statistics.median(x['fps'] for x in rows),median_frame_ms=statistics.median(x['frame_ms'] for x in rows),candidate_calls=delta)
            windows.append(window);print(mode+' median FPS '+str(round(window['median_fps'],2)),flush=True)
    except Exception as error:failure=str(error)
    finally:
        try:request('reference_hold')
        except Exception as error:failure=failure or 'Restore unavailable: '+str(error)
    report=dict(release=release,pid=int(pid),windows=windows,error=failure,restored_mode='reference',app_launched=False,screen_access=False,game_ui_operated=False,measured_on_phone=True,passed=failure is None)
    if not failure:
        reference=[x['median_frame_ms'] for x in windows if x['mode']=='reference'];candidate=[x['median_frame_ms'] for x in windows if x['mode']=='optimized']
        report['reference_drift_ratio']=max(reference)/min(reference)
        report['candidate_drift_ratio']=max(candidate)/min(candidate)
        report['comparable']=max(report['reference_drift_ratio'],report['candidate_drift_ratio'])<=1.10
        report['reference_frame_ms']=statistics.median(reference);report['candidate_frame_ms']=statistics.median(candidate)
        report['speedup']=report['reference_frame_ms']/report['candidate_frame_ms']
        if not report['comparable']:
            failure='Mode windows drifted by more than 10%; no comparable performance conclusion'
            report['error']=failure;report['passed']=False
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(report,indent=2)+'\n')
    if failure:raise RuntimeError(failure)
    print(json.dumps({k:v for k,v in report.items() if k!='windows'},indent=2))

if __name__=='__main__':main()
