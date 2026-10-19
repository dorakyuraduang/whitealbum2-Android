"""Exercise WM_MOUSEACTIVATE in the compiled game, using an isolated save root."""
from pathlib import Path
import argparse
import ctypes as C
from ctypes import wintypes as W
import hashlib
import json
import subprocess
import time
import uuid

ROOT=Path(__file__).resolve().parent.parent

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--program',type=Path,default=ROOT/'out/WA2_SDL2_x64.exe')
    parser.add_argument('--evidence',type=Path,default=ROOT/'logs/mouseactivate-after.json')
    parser.add_argument('--timeout',type=float,default=240)
    args=parser.parse_args()
    program=args.program.resolve()
    evidence=args.evidence.resolve()
    root=Path(json.loads((ROOT/'logs/sdl-smoke-root.json').read_text())['root']).resolve()
    if not root.is_relative_to(ROOT/'build/diagnostic-resources'):
        raise ValueError('Activation diagnostics require an isolated save root')
    for protected in [program,ROOT/'WA2_chs.exe']:
        if evidence==protected or (evidence.exists() and evidence.samefile(protected)):
            raise ValueError('Evidence must not replace an executable')
    report_path=ROOT/'logs'/('diagnostic-'+uuid.uuid4().hex+'.json')
    snapshot=evidence.with_suffix('.bmp')
    command=[str(program),str(ROOT/'WA2_chs.exe'),'--root',str(root),'--budget','8000000000',
             '--report',str(report_path),'--allow-write','--snapshot',str(snapshot),'--frames','200','--profile']
    user=C.WinDLL('user32',use_last_error=True)
    callback=C.WINFUNCTYPE(W.BOOL,W.HWND,W.LPARAM)
    user.EnumWindows.argtypes=[callback,W.LPARAM]
    user.GetWindowThreadProcessId.argtypes=[W.HWND,C.POINTER(W.DWORD)]
    user.GetClassNameW.argtypes=[W.HWND,W.LPWSTR,C.c_int]
    user.IsWindowVisible.argtypes=[W.HWND]
    user.GetWindowRect.argtypes=[W.HWND,C.POINTER(W.RECT)]
    user.SendMessageTimeoutA.argtypes=[W.HWND,W.UINT,C.c_size_t,C.c_ssize_t,W.UINT,W.UINT,C.POINTER(C.c_size_t)]
    user.SendMessageTimeoutA.restype=C.c_ssize_t
    started=time.perf_counter()
    # Drain output directly to disk: Windows anonymous pipes can fill before a
    # final JSON report finishes, preventing poll() from observing child exit.
    console=(ROOT/'logs/mouseactivate-console.log').open('wb')
    process=subprocess.Popen(command,stdout=console,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
    attempts=[]
    completed=set()
    next_attempt={}
    timed_out=False
    try:
        while process.poll() is None:
            if time.perf_counter()-started>args.timeout:
                timed_out=True
                process.kill()
                break
            try:
                raw=json.loads(report_path.read_text())
                state=raw.get('process',raw)
                frames=state.get('direct3d9',{}).get('presents',0) if state else 0
            except (FileNotFoundError,json.JSONDecodeError):
                frames=0
            for threshold in [3,30,100]:
                if frames<threshold or threshold in completed or time.perf_counter()<next_attempt.get(threshold,0):
                    continue
                windows=[]
                @callback
                def owned_window(hwnd,_):
                    pid=W.DWORD()
                    user.GetWindowThreadProcessId(hwnd,C.byref(pid))
                    if pid.value==process.pid:
                        name=C.create_unicode_buffer(256)
                        user.GetClassNameW(hwnd,name,len(name))
                        if name.value.startswith('WinRecomp.') and name.value.endswith('.WHITE ALBUM2CN'):
                            bounds=W.RECT();user.GetWindowRect(hwnd,C.byref(bounds))
                            windows.append((bool(user.IsWindowVisible(hwnd)),(bounds.right-bounds.left)*(bounds.bottom-bounds.top),hwnd))
                    return True
                user.EnumWindows(owned_window,0)
                if not windows:
                    continue
                windows.sort(reverse=True)
                result=C.c_size_t()
                C.set_last_error(0)
                hwnd=windows[0][2]
                delivered=user.SendMessageTimeoutA(hwnd,0x21,hwnd,(0x201<<16)|1,0x22,2000,C.byref(result))
                attempts.append({'threshold':threshold,'presented_frames':frames,'elapsed_seconds':round(time.perf_counter()-started,3),
                                 'native_hwnd':int(hwnd),'visible_window':windows[0][0],
                                 'delivered':bool(delivered),'result':result.value,'native_error':C.get_last_error()})
                if delivered:
                    completed.add(threshold)
                next_attempt[threshold]=time.perf_counter()+1
            time.sleep(0.1)
        process.wait(timeout=10)
    finally:
        if process.poll() is None:
            process.kill()
            process.wait(timeout=10)
        console.close()
    report=json.loads(report_path.read_text())
    if timed_out:
        report={'error':'Activation diagnostic wall-time limit','process':report.get('process',report)}
    record={'schema':'wa2.runtime-evidence.v1','output_sha256':hashlib.sha256(program.read_bytes()).hexdigest(),
            'command':command,'host_return_code':process.returncode,'timed_out':timed_out,
            'elapsed_seconds':round(time.perf_counter()-started,3),'activation_attempts':attempts,'report':report}
    evidence.write_text(json.dumps(record,indent=2)+'\n')
    state=report.get('process',report)
    verified=(process.returncode==0 and not timed_out and not report.get('error') and
              state.get('diagnostic_stop') and state['direct3d9']['presents']==200 and
              state['gui'].get('mouse_activate_callbacks',0)>=3 and
              completed=={3,30,100})
    print(json.dumps({'verified':verified,'error':report.get('error'),'host_return_code':process.returncode,
                      'presented_frames':state.get('direct3d9',{}).get('presents'),
                      'mouse_activate_callbacks':state.get('gui',{}).get('mouse_activate_callbacks'),
                      'activation_attempts':attempts,'evidence':str(evidence)},indent=2))
    return 0 if verified else 2

if __name__=='__main__':
    raise SystemExit(main())
