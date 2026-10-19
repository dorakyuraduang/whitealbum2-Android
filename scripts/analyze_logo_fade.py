"""Summarize only numerical traces produced by trace_logo_fade.py."""
from pathlib import Path
import argparse
import collections
import json


def analyze(path):
    trace=json.loads(path.read_text())
    state=json.loads(Path(trace['runtime']['file']).read_text(encoding='utf-8-sig',errors='replace'))
    state=state.get('process',state)
    timing=state.get('frame_timing',{})
    frames=trace.get('frame_stages') or timing.get('recent',[])
    opacity=trace.get('opacity') or timing.get('opacity',[])
    frames=sorted(frames)
    hz=trace['presentation_clock_hz']
    anchor_counter=timing.get('counter_anchor',0);anchor_ns=timing.get('steady_anchor_ns',0)
    loads=trace.get('resource_loads') or state.get('resource_load_trace',{}).get('recent',[])
    slow=[]
    for a,b in zip(frames,frames[1:]):
        if b[0]!=a[0]+1 or b[0]>1400:continue
        duration=(b[1]-a[1])*1000/hz
        if duration<8:continue
        begin_ns=anchor_ns+(a[1]-anchor_counter)*1e9/hz
        end_ns=anchor_ns+(b[1]-anchor_counter)*1e9/hz
        work=[dict(member=v[3],ms=v[1]/1e6,bytes=v[2]) for v in loads if begin_ns<=v[0]<=end_ns]
        slow.append(dict(frame=b[0],interval_ms=duration,draw_ms=b[3]/1e6,apply_ms=b[4]/1e6,
                         flush_ms=b[5]/1e6,upload_ms=b[6]/1e6,copy_ms=b[7]/1e6,
                         present_ms=b[8]/1e6,error_ms=b[9]/1e6,swap_ms=b[10]/1e6,
                         draws=b[11],uploads=b[12],resource_work=work))
    groups=collections.defaultdict(list)
    for row in opacity:groups[row[2]].append(row)
    fades=[]
    for texture,rows in groups.items():
        if len({v[5] for v in rows})<5:continue
        changes=[]
        for a,b in zip(rows,rows[1:]):
            if a[5]==b[5]:continue
            changes.append(dict(frame=b[0],ms=(b[1]-a[1])*1000/hz,alpha_before=a[5],alpha=b[5]))
        fades.append(dict(texture=texture,changes=len(changes),first_frame=rows[0][0],last_frame=rows[-1][0],
                          first=rows[0],last=rows[-1],updates=changes[:45],
                          maximum_update_gap_ms=max((v['ms'] for v in changes),default=0)))
    return dict(trace=str(path),sha256=trace['output_sha256'],frames=len(frames),opacity_events=len(opacity),
                slow_frames=slow[:35],fades=fades[:16],movie=state.get('directshow'),error=trace['error'])


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace',type=Path);parser.add_argument('--output',type=Path)
    args=parser.parse_args();result=analyze(args.trace)
    output=json.dumps(result,indent=2,ensure_ascii=True)+'\n'
    if args.output:args.output.write_text(output)
    print(output)


if __name__=='__main__':main()
