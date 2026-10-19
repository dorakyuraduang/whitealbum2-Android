"""Bind sustained performance and original-menu pixel comparisons to a run."""
from pathlib import Path
import argparse
import hashlib
import json
import statistics
from PIL import Image, ImageChops, ImageStat

ROOT=Path(__file__).resolve().parent.parent

def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def rates(samples):
    result=[]
    for start,end in [(0,15),(15,30),(30,45),(45,60),(60,90),(90,120)]:
        rows=[s for s in samples if start<=s['seconds_after_menu']<=end]
        if len(rows)<2 or rows[-1]['seconds_after_menu']-rows[0]['seconds_after_menu']<end-start-2:
            continue
        first,last=rows[0],rows[-1]
        seconds=last['elapsed_seconds']-first['elapsed_seconds']
        frames=last['presents']-first['presents']
        result.append({'menu_seconds':[start,end],'observed_seconds':seconds,'presents':frames,'fps':frames/seconds,
                       'foreground_samples':sum(r.get('foreground_game',False) for r in rows),'samples':len(rows)})
    return result

def precise_frames(run):
    frequency=run.get('presentation_clock_hz')
    samples=run.get('steady_samples',[])
    if not frequency or not samples:return None
    first,last=samples[0]['presents'],samples[-1]['presents']
    frames=sorted(r for r in run.get('presentation_times',[]) if first<=r[0]<=last)
    groups={'all_visible':[],'foreground':[],'background_visible':[],'minimized':[]}
    for a,b in zip(frames,frames[1:]):
        if b[0]!=a[0]+1 or b[1]<=a[1]:continue
        ms=1000*(b[1]-a[1])/frequency
        if a[3] or b[3]:groups['minimized'].append(ms)
        else:
            groups['all_visible'].append(ms)
            if a[2] and b[2]:groups['foreground'].append(ms)
            elif not a[2] and not b[2]:groups['background_visible'].append(ms)
    result={}
    for name,values in groups.items():
        if not values:continue
        ordered=sorted(values)
        result[name]={'intervals':len(values),'seconds':sum(values)/1000,'fps':1000/statistics.mean(values),
                      'median_frame_ms':statistics.median(values),'p95_frame_ms':ordered[min(len(ordered)-1,int(len(ordered)*.95))],
                      'maximum_frame_ms':max(values)}
    return result

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--launch-report',type=Path,required=True)
    parser.add_argument('--baseline',type=Path)
    parser.add_argument('--output',type=Path,default=ROOT/'logs/sustained-launch-verification.json')
    args=parser.parse_args()
    run=json.loads(args.launch_report.read_text(encoding='utf-8-sig'))
    case=Path(run['case'])
    if run['launch_mode']=='profile':raise ValueError('A profiled run is not normal-play performance evidence')
    if run['forced_diagnostic_cleanup'] or run['launcher_return_code'] or not run['close_requested']:
        raise ValueError('Run did not close normally')
    if run['menu_observation_seconds']<45:raise ValueError('At least 45 seconds of menu observation are required')
    samples=run['steady_samples']
    if samples[-1]['seconds_after_menu']-samples[0]['seconds_after_menu']<43:
        raise ValueError('Insufficient actual observation time')
    reference=run.get('menu_reference',{'file':str(ROOT/'build/original-startup/a35401246fe04e969c4604d7cdb7756c/menu.png')})
    original=Image.open(reference['file']).convert('RGB')
    choices=[case/f'client-menu-plus-{n}.bmp' for n in (60,45)]
    shot=next((p for p in choices if p.exists()),None)
    if shot is None:raise ValueError('No sustained client screenshot was captured')
    picture=Image.open(shot).convert('RGB')
    if picture.size!=original.size:raise ValueError('Client size differs from the native reference')
    regions={'title':(270,160,1020,255),'menu_buttons':(510,350,775,600),'footer_text':(495,624,785,650)}
    differences={name:sum(ImageStat.Stat(ImageChops.difference(picture.crop(rect),original.crop(rect))).mean)/3
                 for name,rect in regions.items()}
    if any(error>2 for error in differences.values()):raise ValueError('Menu differs from original reference: '+str(differences))
    proof={'schema':'wa2.sustained-launch.v1','output_sha256':run['output_sha256'],
           'launch_mode':run['launch_mode'],'native_console':run['native_console'],
           'launch_report':{'file':str(args.launch_report.resolve()),'sha256':digest(args.launch_report)},
           'runtime_report':{'file':str(case/'runtime.json'),'sha256':digest(case/'runtime.json')},
           'menu_screenshot':{'file':str(shot),'sha256':digest(shot)},
           'native_reference':{'file':reference['file'],'sha256':digest(reference['file'])},
           'menu_region_mean_pixel_errors':differences,'normal_exit_verified':True,
           'menu_observation_seconds':run['menu_observation_seconds'],'sampled_rates':rates(samples),
           'precise_present_intervals':precise_frames(run),'full_gameplay_verified':False}
    # A matching menu image establishes rendering correctness, not playability.
    # Expose the measured performance gate explicitly instead of letting a
    # successful script exit imply that the game now runs smoothly.
    intervals=proof['precise_present_intervals'] or {}
    visible=intervals.get('all_visible',{})
    late=[row for row in proof['sampled_rates'] if row['menu_seconds'][0]>=30]
    proof['sustained_30fps'] = bool(late and all(row['fps']>=30 for row in late)
                                  and visible.get('fps',0)>=30
                                  and visible.get('p95_frame_ms',float('inf'))<=50)
    if args.baseline:
        baseline=json.loads(args.baseline.read_text(encoding='utf-8-sig'))
        proof['baseline']={'output_sha256':baseline['output_sha256'],'launch_report':str(args.baseline.resolve()),
                           'launch_report_sha256':digest(args.baseline),'sampled_rates':rates(baseline['steady_samples'])}
    args.output.write_text(json.dumps(proof,indent=2)+'\n')
    print(json.dumps(proof,indent=2))

if __name__=='__main__':main()
