"""Bind Logo fade timing to owned-process reports, without visual assertions."""
from pathlib import Path
import argparse
import hashlib
import json
ROOT=Path(__file__).resolve().parents[1]
def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def bound(path):return dict(file=str(Path(path).resolve()),sha256=digest(path))
def read(path):return json.loads(Path(path).read_text(encoding='utf-8-sig'))

def opening(path,require_background=True):
    trace=read(path)
    assert trace['frame_trace'] and not trace['error'] and trace['alive_at_test_stop']
    assert not trace['screen_access'] and not trace['automated_input']
    assert digest(trace['program'])==trace['output_sha256']
    assert digest(trace['runtime']['file'])==trace['runtime']['sha256']
    state=read(trace['runtime']['file']);state=state.get('process',state)
    assert state['direct3d9']['frames_captured']==0
    frames={v[0]:v for v in trace['frame_stages']}
    first=next(v for v in trace['opacity'] if 0<v[5]<255)
    frame=frames[first[0]];previous=frames[first[0]-1]
    visibility=next(v for v in trace['frames'] if v[0]==first[0])
    assert visibility[3]==0
    if require_background:assert visibility[2:]==[0,0]
    assert first[3:6]==[256,256,4] # Same first fade step and texture tile size.
    hz=trace['presentation_clock_hz']
    steps=[v[5] for v in trace['opacity'] if v[2]==first[2]]
    return trace,state,dict(evidence=bound(path),output_sha256=trace['output_sha256'],
        first_logo_frame=first[0],first_logo_interval_ms=(frame[1]-previous[1])*1000/hz,
        foreground=bool(visibility[2]),minimized=bool(visibility[3]),
        initial_vertex_alpha=first[5],initial_alpha_steps=steps[:8],texture_upload_ms=frame[6]/1e6,
        present_ms=frame[8]/1e6,swap_ms=frame[10]/1e6)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--before',type=Path,required=True)
    parser.add_argument('--diagnostic',type=Path,required=True)
    parser.add_argument('--after',type=Path,action='append',required=True)
    parser.add_argument('--program',type=Path,required=True)
    args=parser.parse_args()
    before,_,old=opening(args.before)
    diagnostic,_,_=opening(args.diagnostic)
    first=next(v for v in diagnostic['opacity'] if 0<v[5]<255)
    stages={v[0]:v for v in diagnostic['frame_stages']}
    a=stages[first[0]-1][1];b=stages[first[0]][1]
    waits=[v for v in diagnostic['api_latencies'] if a<=v['counter']<=b and v['kind']=='after_api' and v['detail'].startswith('BGM.PAK:4096')]
    assert len(waits)>=3 and all(v['elapsed_ns']>5_000_000 for v in waits)
    value=digest(args.program);after=[]
    for path in args.after:
        trace,state,result=opening(path,require_background=False)
        assert trace['output_sha256']==value
        assert 'api_latency' not in state and 'runtime_report_timing' not in state
        codec=state['native_vorbis'];assert codec['calls']>0 and codec['output_bytes']>0 and codec['errors']==codec['declined']==0
        assert result['first_logo_interval_ms']<16.667
        if result['foreground']==old['foreground']:
            assert result['first_logo_interval_ms']<old['first_logo_interval_ms']*.6
        assert result['initial_alpha_steps'][:4]==[4,8,12,17]
        result['native_codec']=codec;after.append(result)
    assert len(after)>=2
    same_visibility=all(v['foreground']==old['foreground'] and v['minimized']==old['minimized'] for v in after)
    proof=dict(schema='wa2.logo-fade-fix.v1',output_sha256=value,before=old,after=after,
        causal_diagnostic=bound(args.diagnostic),bgm_cpu_intervals=waits,
        same_foreground_conditions=same_visibility,
        scope='Current first-logo interval and fade-step regression. Historical latency is directly comparable only when foreground/minimized conditions match; no screen refresh or visual smoothness claim',
        screen_access=False,automated_input=False,visual_smoothness_verified=False,
        original_performance_proven=False,normal_exit_verified=False,passed=True)
    (ROOT/'logs/logo-fade-verification.json').write_text(json.dumps(proof,indent=2)+'\n')
    print(json.dumps(dict(historical_before_ms=old['first_logo_interval_ms'],after_ms=[v['first_logo_interval_ms'] for v in after],same_foreground_conditions=same_visibility,passed=True),indent=2))

if __name__=='__main__':main()
