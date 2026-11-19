"""Compare reference/optimized CPU text paths in an already running phone game.

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
CANDIDATE='glyph-layout-and-commands'

def candidate_work(mode, first, last):
    work={}
    for key, exclusive in (('layout_hits','layout'),('glyph_command_hits','commands')):
        delta=last[key]-first[key]
        if first[key]<0 or delta<0:
            raise ValueError('Missing or reset candidate counter: '+key)
        if mode not in ('optimized',exclusive) and delta:
            raise ValueError('Disabled path ran during comparison: '+key)
        work[key]=delta
    if mode!='reference' and not any(work.values()):
        raise ValueError('No candidate work was admitted in this scene')
    return work

def glyph_work(previous, current):
    frames=current['frames']-previous['frames']
    if frames<=0:
        raise ValueError('Frame counters stopped or reset during the comparison')
    work={}
    for key in ('glyphs','glyph_tiles'):
        delta=current[key]-previous[key]
        if delta<=0:
            raise ValueError('Missing or reset text workload during the comparison: '+key)
        work[key+'_per_frame']=delta/frames
    return work

def validate_workload(intervals):
    for key in ('glyphs_per_frame','glyph_tiles_per_frame'):
        smallest=min(row[key] for row in intervals)
        largest=max(row[key] for row in intervals)
        if largest-smallest>max(1.0,smallest*.02):
            raise ValueError(f'Text workload changed: {key} ranged from {smallest:.2f} to {largest:.2f}')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial',required=True)
    parser.add_argument('--samples',type=int,choices=range(4,13),default=8)
    parser.add_argument('--path',choices=('optimized','layout','commands'),default='optimized')
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
            if record.get('version')==release and record.get('candidate')==CANDIDATE:records.append(record)
        if not records:raise RuntimeError('No live performance response; game must be open and foreground')
        record=records[-1]
        expected='reference' if mode=='reference_hold' else mode
        if record['uptime_ms']<=last_response or (mode!='snapshot' and record.get('mode')!=expected):
            raise RuntimeError('No fresh performance response from the foreground game')
        last_response=record['uptime_ms'];return record
    windows=[];workload_intervals=[];failure=None;restore_error=None;restored_mode=None
    try:
        for mode in ('reference',args.path,args.path,'reference'):
            rows=[]
            window=dict(mode=mode,samples=rows,glyph_work=[],complete=False)
            windows.append(window)
            first=request(mode);start=first['uptime_ms'];time.sleep(1.1)
            for _ in range(args.samples):
                row=request('snapshot')
                if not row.get('valid') or not row.get('foreground') or not row.get('focused') or row.get('mode')!=mode:
                    raise RuntimeError('No comparable foreground frame sample')
                if row['uptime_ms']-row['sample_age_ms']<start+500:raise RuntimeError('Frame sample predates the mode change')
                rows.append(row)
                if len(rows)>=2:
                    interval=glyph_work(rows[-2],row)
                    window['glyph_work'].append(interval)
                    workload_intervals.append(interval)
                    validate_workload(workload_intervals)
                time.sleep(.85)
            # A text layout call that was already in flight when the switch
            # arrived belongs to the warm-up interval.
            work=candidate_work(mode,rows[0],rows[-1])
            window.update(median_fps=statistics.median(x['fps'] for x in rows),median_frame_ms=statistics.median(x['frame_ms'] for x in rows),candidate_work=work,complete=True)
            print(mode+' median FPS '+str(round(window['median_fps'],2)),flush=True)
    except Exception as error:failure=str(error)
    finally:
        # Restore the shipped default. If foreground delivery is
        # unavailable, never report a restoration which wasn't acknowledged.
        try:restored_mode=request('optimized')['mode']
        except Exception as error:
            restore_error=str(error);failure=failure or 'Restore unavailable: '+restore_error
    report=dict(release=release,candidate=CANDIDATE,path=args.path,pid=int(pid),windows=windows,error=failure,restored_mode=restored_mode,restoration_verified=restored_mode=='optimized',restore_error=restore_error,app_launched=False,screen_access=False,game_ui_operated=False,measured_on_phone=True,comparable=False,passed=failure is None)
    if not failure:
        reference=[x['median_frame_ms'] for x in windows if x['mode']=='reference'];candidate=[x['median_frame_ms'] for x in windows if x['mode']==args.path]
        report['reference_drift_ratio']=max(reference)/min(reference)
        report['candidate_drift_ratio']=max(candidate)/min(candidate)
        report['comparable']=max(report['reference_drift_ratio'],report['candidate_drift_ratio'])<=1.10
        report['reference_frame_ms']=statistics.median(reference);report['candidate_frame_ms']=statistics.median(candidate)
        if not report['comparable']:
            failure='Mode windows drifted by more than 10%; no comparable performance conclusion'
            report['error']=failure;report['passed']=False
        else:
            report['speedup']=report['reference_frame_ms']/report['candidate_frame_ms']
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(report,indent=2)+'\n')
    if failure:raise RuntimeError(failure)
    print(json.dumps({k:v for k,v in report.items() if k!='windows'},indent=2))

if __name__=='__main__':main()
