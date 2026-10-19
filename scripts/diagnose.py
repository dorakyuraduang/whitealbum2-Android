"""Run only the recompiled x64 output and retain its exact build identity."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import time
import uuid

ROOT=Path(__file__).resolve().parent.parent

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--root',type=Path,default=ROOT/'build'/'no-assets')
    parser.add_argument('--budget',type=int,default=50000000)
    parser.add_argument('--timeout',type=float,default=45)
    parser.add_argument('--allow-write',action='store_true')
    parser.add_argument('--snapshot',type=Path)
    parser.add_argument('--frames',type=int)
    parser.add_argument('--program',type=Path,default=ROOT/'out'/'WA2_SDL2_x64.exe')
    parser.add_argument('--evidence',type=Path,default=ROOT/'logs'/'runtime-evidence.json')
    parser.add_argument('--profile',action='store_true')
    args=parser.parse_args()
    if args.budget<=0:raise ValueError('Budget must be positive')
    if args.timeout<=0:raise ValueError('Timeout must be positive')
    args.root.mkdir(parents=True,exist_ok=True)
    native=args.program.resolve()
    args.evidence=args.evidence.resolve()
    for protected in [native,ROOT/'WA2_chs.exe']:
        if args.evidence==protected.resolve() or (args.evidence.exists() and protected.exists() and args.evidence.samefile(protected)):
            raise ValueError('Evidence must not overwrite an executable, including a hard link')
    if args.snapshot and args.snapshot.resolve()==args.evidence:raise ValueError('Snapshot and evidence must use different files')
    report_file=ROOT/'logs'/('diagnostic-'+uuid.uuid4().hex+'.json')
    command=[str(native),str(ROOT/'WA2_chs.exe'),'--root',str(args.root.resolve()),'--budget',str(args.budget),'--report',str(report_file)]
    if args.allow_write:command.append('--allow-write')
    if args.snapshot:command+=['--snapshot',str(args.snapshot.resolve())]
    if args.frames:command+=['--frames',str(args.frames)]
    if args.profile:command.append('--profile')
    started=time.perf_counter()
    timed_out=False
    try:
        result=subprocess.run(command,capture_output=True,timeout=args.timeout)
        stdout,stderr,returncode=result.stdout,result.stderr,result.returncode
    except subprocess.TimeoutExpired as error:
        stdout,stderr,returncode=error.stdout or b'',error.stderr or b'',124
        timed_out=True
    (ROOT/'logs'/'runtime-console.log').write_text((stdout+stderr).decode('utf-8',errors='replace'),encoding='utf-8')
    if not report_file.exists():raise RuntimeError(f'Native output did not produce a report; exit={returncode}')
    report=json.loads(report_file.read_text())
    if timed_out:report={'error':f'Diagnostic wall-time limit ({args.timeout:g}s); process terminated by the diagnostic harness.','process':report.get('process',report)}
    record={'schema':'wa2.runtime-evidence.v1','output_sha256':hashlib.sha256(native.read_bytes()).hexdigest(),'command':command,'host_return_code':returncode,'timed_out':timed_out,'elapsed_seconds':round(time.perf_counter()-started,3),'report':report}
    args.evidence.parent.mkdir(parents=True,exist_ok=True)
    args.evidence.write_text(json.dumps(record,indent=2)+'\n')
    state=report.get('process',report)
    print(json.dumps({'host_return_code':returncode,'error':report.get('error'),'native_instructions':state.get('native_instructions'),'api_calls':state.get('api_calls'),'guest_eip':hex(state.get('eip',0)),'direct3d9':state.get('direct3d9'),'directsound':state.get('directsound'),'last_transfers':state.get('recent_transfers',[])[-6:]},indent=2))
    return returncode

if __name__=='__main__':raise SystemExit(main())
