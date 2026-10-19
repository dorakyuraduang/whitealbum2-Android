"""Launch the staged standalone EXE; inspect only its own report, never UI."""
from pathlib import Path
import hashlib
import argparse
import json
import subprocess
import time
import uuid

ROOT=Path(__file__).resolve().parents[1]

def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--seconds',type=float,default=15);args=parser.parse_args()
    if args.seconds<15:raise ValueError('Startup observation must be at least 15 seconds')
    program=ROOT/'res/WA2_SDL2_x64.exe';value=digest(program)
    if value!=digest(ROOT/'out/WA2_SDL2_x64.exe'):raise ValueError('Resource directory contains another build')
    identity=uuid.uuid4().hex[:12]
    report=ROOT/'logs'/('fixed-startup-'+identity+'.json');console=ROOT/'logs'/('fixed-startup-'+identity+'.log')
    # SW_SHOWNOACTIVATE follows the user's request to continue background runs.
    # No foreground calls, input, window queries, screen reads or FPS analysis.
    startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=4
    started=time.monotonic();latest=None;alive=False
    with console.open('wb') as output:
        process=subprocess.Popen([str(program),'--report',str(report)],cwd=ROOT,
            stdout=output,stderr=subprocess.STDOUT,startupinfo=startup)
        try:
            while time.monotonic()-started<args.seconds and process.poll() is None:
                try:latest=json.loads(report.read_text(encoding='utf-8-sig'))
                except (OSError,ValueError):pass
                if latest and latest.get('error'):break
                time.sleep(.25)
            alive=process.poll() is None
        finally:
            if process.poll() is None:process.terminate()
            process.wait(timeout=10)
    try:latest=json.loads(report.read_text(encoding='utf-8-sig'))
    except (OSError,ValueError):pass
    result=dict(schema='wa2.fixed-startup.v1',output_sha256=value,program=str(program),working_directory=str(ROOT),
        explicit_root_argument=False,requested_show_window=4,elapsed_seconds=time.monotonic()-started,
        alive_at_test_stop=alive,ended_by_test_timer=alive,error=latest.get('error') if latest else 'missing runtime report',
        report=dict(file=str(report),sha256=digest(report)) if report.exists() else None,
        console=dict(file=str(console),sha256=digest(console)),screen_access=False,automated_input=False,
        fps_analysis=False,actual_gameplay_fast_forward_verified=False,normal_exit_verified=False)
    result['requested_seconds']=args.seconds
    (ROOT/'logs/fixed-startup-verification.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))
    if result['error'] or not alive or result['elapsed_seconds']<args.seconds:raise RuntimeError('Standalone startup check failed')

if __name__=='__main__':main()
