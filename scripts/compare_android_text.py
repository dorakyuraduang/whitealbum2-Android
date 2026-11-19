"""Compare rendering paths in an already running phone game.

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
CANDIDATE='shared-render-paths'

def candidate_work(mode, first, last, reference=False):
    work={}
    pages={'page','bindings','dense','novel'}
    paths={
        'run_hits':{'run','optimized'},
        'surface_hits':pages | {'submission','optimized','page_reference'},
        'rollover_hits':pages | {'submission','optimized','page_reference'},
        'page_glyphs':pages,'page_spans':pages,
        'prepared_geometry_hits':pages,'retained_geometry_draws':pages,
        'binding_calls_avoided':{'bindings','novel'}
    }
    for key, enabled in paths.items():
        delta=last[key]-first[key]
        if first[key]<0 or delta<0:
            raise ValueError('Missing or reset candidate counter: '+key)
        if mode not in enabled and delta:
            raise ValueError('Disabled path ran during comparison: '+key)
        work[key]=delta
    if mode=='bindings':admitted=work['binding_calls_avoided']
    elif mode=='novel':admitted=work['page_glyphs']+work['binding_calls_avoided']
    elif mode in pages:admitted=work['page_glyphs']
    else:admitted=sum(work.values())
    if not reference and not admitted:
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

def particle_work(previous,current,reference):
    frames=current['frames']-previous['frames']
    if frames<=0:
        raise ValueError('Particle frame counters stopped or reset')
    if previous.get('limit_60_fps') is not False or current.get('limit_60_fps') is not False:
        raise ValueError('Turn off the 60 FPS limit before measuring particle throughput')
    commands=current['particle_commands']-previous['particle_commands']
    tiles=current['particle_tiles']-previous['particle_tiles']
    hits=current['particle_continuations']-previous['particle_continuations']
    # Accepted sprites with empty texture slots can legitimately contribute no tiles.
    if commands<=0 or tiles<=0 or not 0<=hits<=min(commands,tiles):
        raise ValueError('Missing, reset or inconsistent particle submissions')
    if reference and hits:
        raise ValueError('Particle continuation ran in the reference window')
    return dict(commands_per_frame=commands/frames,tiles_per_frame=tiles/frames,
                submitted=commands,tiles=tiles,continued=hits)

def validate_particles(intervals):
    for key in ('commands_per_frame','tiles_per_frame'):
        smallest=min(row[key] for row in intervals)
        largest=max(row[key] for row in intervals)
        if largest-smallest>max(.25,smallest*.10):
            raise ValueError(f'Particle workload changed: {key} ranged from {smallest:.2f} to {largest:.2f}')

def weather_work(first,last,reference):
    elapsed_ms=(last['uptime_ms']-last['sample_age_ms'])-(first['uptime_ms']-first['sample_age_ms'])
    frames=last['frames']-first['frames']
    updates=last['small_weather_updates']-first['small_weather_updates']
    particles=last['small_weather_particles']-first['small_weather_particles']
    batched=last['small_weather_batched_updates']-first['small_weather_batched_updates']
    if elapsed_ms<=0 or frames<=0 or updates<3 or not updates<=particles<=63*updates or not 0<=batched<=updates:
        raise ValueError('Insufficient active small-weather simulation during the window')
    if reference and batched:
        raise ValueError('The new weather threshold ran during the reference window')
    if not reference and not batched:
        raise ValueError('The new weather threshold did not complete any eligible update')
    seconds=elapsed_ms/1000
    return dict(updates=updates,simulated_particles=particles,batched_updates=batched,
                seconds=seconds,frames=frames,updates_per_frame=updates/frames,particles_per_frame=particles/frames,
                updates_per_second=updates/seconds,particles_per_second=particles/seconds,
                particles_per_update=particles/updates)

def validate_weather(windows):
    # One animation tick can fall either side of a sample boundary.
    max_population=max(w['particles_per_update'] for w in windows)
    def consistent(key,tick):
        smallest=min(w[key] for w in windows)
        largest=max(w[key] for w in windows)
        return largest-smallest<=max(tick,smallest*.10)
    if not consistent('particles_per_update',1):
        raise ValueError('Weather particle population changed between windows')
    matching=[]
    for unit,denominator in [('second','seconds'),('frame','frames')]:
        shortest=min(w[denominator] for w in windows)
        if consistent('updates_per_'+unit,1/shortest) and consistent('particles_per_'+unit,max_population/shortest):
            matching.append('per_'+unit)
    if not matching:
        raise ValueError('Weather work matched neither per second nor per rendered frame across all windows')
    return matching

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial',required=True)
    parser.add_argument('--samples',type=int,choices=range(4,13),default=8)
    parser.add_argument('--path',choices=('novel','dense','bindings','page','optimized','run','submission','particles','weather'),default='novel')
    parser.add_argument('--output',type=Path)
    args=parser.parse_args();low_priority()
    weather=args.path=='weather'
    particles=weather or args.path=='particles'
    work_key='particle_work' if particles else 'glyph_work'
    if args.output is None:
        name='weather' if weather else 'particle' if particles else 'text'
        args.output=ROOT/'logs'/f'android-{name}-comparison.json'
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
        text=call('logcat','-d','--uid='+uid[1],'--pid='+pid,'-v','brief','WA2PERF:I','*:S')
        if len(text.encode('utf-8'))>1024*1024:raise RuntimeError('Own performance log exceeds the bounded reader')
        records=[]
        for line in text.splitlines()[-128:]:
            try:record=json.loads(line[line.index('{'):])
            except (ValueError,IndexError):continue
            if record.get('version')==release and record.get('candidate')==CANDIDATE:records.append(record)
        if not records:raise RuntimeError('No live performance response; game must be open and foreground')
        record=records[-1]
        if record['uptime_ms']<=last_response or (mode!='snapshot' and record.get('mode')!=mode):
            raise RuntimeError('No fresh performance response from the foreground game')
        last_response=record['uptime_ms'];return record
    windows=[];workload_intervals=[];failure=None;restore_error=None;restored_mode=None
    try:
        if weather:reference_mode='weather_reference'
        elif particles:reference_mode='particle_reference'
        elif args.path in ('novel','dense','bindings'):reference_mode='page'
        else:reference_mode='page_reference' if args.path=='page' else 'reference'
        for mode in (reference_mode,args.path,args.path,reference_mode):
            rows=[]
            window=dict(mode=mode,samples=rows,complete=False)
            window[work_key]=[]
            windows.append(window)
            first=request(mode);start=first['uptime_ms'];time.sleep(1.1)
            for _ in range(args.samples):
                row=request('snapshot')
                if not row.get('valid') or not row.get('foreground') or not row.get('focused') or row.get('mode')!=mode:
                    raise RuntimeError('No comparable foreground frame sample')
                if row['uptime_ms']-row['sample_age_ms']<start+500:raise RuntimeError('Frame sample predates the mode change')
                rows.append(row)
                if len(rows)>=2:
                    interval=particle_work(rows[-2],row,mode=='particle_reference') if particles else glyph_work(rows[-2],row)
                    window[work_key].append(interval)
                    workload_intervals.append(interval)
                    if particles:validate_particles(workload_intervals)
                    else:validate_workload(workload_intervals)
                time.sleep(.85)
            # A text layout call that was already in flight when the switch
            # arrived belongs to the warm-up interval.
            if particles:
                work=particle_work(rows[0],rows[-1],mode=='particle_reference')
                if weather:
                    simulation=weather_work(rows[0],rows[-1],mode==reference_mode)
                    window['weather_work']=simulation
                elif mode!=reference_mode and not work['continued']:
                    raise ValueError('No particle continuation was used in this scene')
            else:work=candidate_work(mode,rows[0],rows[-1],mode==reference_mode)
            window.update(median_fps=statistics.median(x['fps'] for x in rows),median_frame_ms=statistics.median(x['frame_ms'] for x in rows),candidate_work=work,complete=True)
            measured=[sample.get('window') for sample in rows]
            if all(sample and sample.get('game_thread_cpu_ns') is not None and sample['frames']>0 for sample in measured):
                window['game_thread_ms_per_frame']=sum(sample['game_thread_cpu_ns'] for sample in measured)/sum(sample['frames'] for sample in measured)/1e6
            print(mode+' median FPS '+str(round(window['median_fps'],2)),flush=True)
        if weather:weather_cadence=validate_weather([w['weather_work'] for w in windows])
    except Exception as error:failure=str(error)
    finally:
        # Restore the shipped default. If foreground delivery is
        # unavailable, never report a restoration which wasn't acknowledged.
        try:restored_mode=request('default')['mode']
        except Exception as error:
            restore_error=str(error);failure=failure or 'Restore unavailable: '+restore_error
    report=dict(release=release,candidate=CANDIDATE,path=args.path,pid=int(pid),windows=windows,error=failure,restored_mode=restored_mode,restoration_verified=restored_mode=='default',restore_error=restore_error,app_launched=False,screen_access=False,game_ui_operated=False,measured_on_phone=True,comparable=False,passed=failure is None)
    if not failure:
        if weather:report['weather_workload_matches']=weather_cadence
        reference=[x['median_frame_ms'] for x in windows if x['mode']==reference_mode];candidate=[x['median_frame_ms'] for x in windows if x['mode']==args.path]
        report['reference_drift_ratio']=max(reference)/min(reference)
        report['candidate_drift_ratio']=max(candidate)/min(candidate)
        report['comparable']=max(report['reference_drift_ratio'],report['candidate_drift_ratio'])<=1.10
        report['reference_frame_ms']=statistics.median(reference);report['candidate_frame_ms']=statistics.median(candidate)
        if all('game_thread_ms_per_frame' in window for window in windows):
            for mode,key in ((reference_mode,'reference'),(args.path,'candidate')):
                report[key+'_game_thread_ms']=statistics.median(window['game_thread_ms_per_frame'] for window in windows if window['mode']==mode)
        if not report['comparable']:
            failure='Mode windows drifted by more than 10%; no comparable performance conclusion'
            report['error']=failure;report['passed']=False
        else:
            report['speedup']=report['reference_frame_ms']/report['candidate_frame_ms']
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(report,indent=2)+'\n')
    if failure:raise RuntimeError(failure)
    print(json.dumps({k:v for k,v in report.items() if k!='windows'},indent=2))

if __name__=='__main__':main()
