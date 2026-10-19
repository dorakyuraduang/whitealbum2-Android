"""Time only an owned original game process using ETW Present events.

No screen pixels, screenshots, input events, window text, or automated input.
Game resources are hardlinked into an isolated local working directory.
"""
from pathlib import Path
import argparse
import csv
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
import shutil
import subprocess
import time
import uuid
from process_metrics import cpu_seconds,memory_counters,qpc,qpc_hz,original_crt_features

ROOT=Path(__file__).resolve().parents[1]
MONITOR=ROOT/'tools/PresentMon-2.6.0-x64.exe'
MONITOR_SHA='b2a706bc6ad475749e3b7e3409263aa1e6906d45bdcf993f6dbc0f660188f1af'


def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()


class StartupInfo(ctypes.Structure):
    _fields_=[('cb',wintypes.DWORD),('lpReserved',wintypes.LPWSTR),('lpDesktop',wintypes.LPWSTR),('lpTitle',wintypes.LPWSTR),
        *[(name,wintypes.DWORD) for name in ('dwX','dwY','dwXSize','dwYSize','dwXCountChars','dwYCountChars','dwFillAttribute','dwFlags')],
        ('wShowWindow',wintypes.WORD),('cbReserved2',wintypes.WORD),('lpReserved2',ctypes.POINTER(wintypes.BYTE)),
        ('hStdInput',wintypes.HANDLE),('hStdOutput',wintypes.HANDLE),('hStdError',wintypes.HANDLE)]


class ProcessInfo(ctypes.Structure):
    _fields_=[('process',wintypes.HANDLE),('thread',wintypes.HANDLE),('pid',wintypes.DWORD),('tid',wintypes.DWORD)]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--seconds',type=int,default=100)
    parser.add_argument('--evidence',type=Path,required=True)
    parser.add_argument('--profile',type=Path)
    parser.add_argument('--background',action='store_true',help='Request SW_SHOWNOACTIVATE at process creation; do not change focus after launch')
    args=parser.parse_args()
    def profile_hashes():
        return {name:digest(args.profile/name) for name in ('D3D.ini','SYSTEM.ini','Sys.sav')} if args.profile else None
    profile_before=profile_hashes()
    if not 10<=args.seconds<=300:raise ValueError('Measurement must be 10..300 seconds')
    if digest(MONITOR)!=MONITOR_SHA:raise ValueError('Official PresentMon checksum mismatch')
    evidence=args.evidence.resolve()
    if evidence in ((ROOT/'WA2_chs.exe').resolve(),MONITOR.resolve()):raise ValueError('Evidence must not overwrite a program')
    case=ROOT/'build/original-performance'/uuid.uuid4().hex;case.mkdir(parents=True)
    source=ROOT/'res';copied=[];linked=[]
    for path in source.rglob('*'):
        if not path.is_file() or path.suffix.lower() in ('.exe','.cmd','.json','.log'):continue
        if not path.resolve().is_relative_to(source.resolve()):raise ValueError('Resource link escapes the resource directory')
        target=case/path.relative_to(source);target.parent.mkdir(parents=True,exist_ok=True)
        if path.suffix.lower()=='.pak':os.link(path,target);linked.append(str(path.relative_to(source)))
        else:shutil.copy2(path,target);copied.append(dict(file=str(path.relative_to(source)),sha256=digest(path)))
    program=case/'WA2_chs.exe';shutil.copy2(ROOT/'WA2_chs.exe',program)
    kernel=ctypes.WinDLL('kernel32',use_last_error=True);user=ctypes.WinDLL('user32',use_last_error=True)
    kernel.CreateProcessW.argtypes=[wintypes.LPCWSTR,wintypes.LPWSTR,ctypes.c_void_p,ctypes.c_void_p,wintypes.BOOL,wintypes.DWORD,ctypes.c_void_p,wintypes.LPCWSTR,ctypes.POINTER(StartupInfo),ctypes.POINTER(ProcessInfo)]
    kernel.CreateProcessW.restype=wintypes.BOOL
    kernel.ResumeThread.argtypes=[wintypes.HANDLE];kernel.ResumeThread.restype=wintypes.DWORD
    kernel.WaitForSingleObject.argtypes=[wintypes.HANDLE,wintypes.DWORD]
    kernel.TerminateProcess.argtypes=[wintypes.HANDLE,wintypes.UINT]
    kernel.GetExitCodeProcess.argtypes=[wintypes.HANDLE,ctypes.POINTER(wintypes.DWORD)]
    kernel.CloseHandle.argtypes=[wintypes.HANDLE]
    user.GetForegroundWindow.restype=wintypes.HWND
    user.GetWindowThreadProcessId.argtypes=[wintypes.HWND,ctypes.POINTER(wintypes.DWORD)]
    user.IsIconic.argtypes=[wintypes.HWND];user.IsIconic.restype=wintypes.BOOL
    user.IsWindow.argtypes=[wintypes.HWND];user.IsWindow.restype=wintypes.BOOL
    user.IsWindowVisible.argtypes=[wintypes.HWND];user.IsWindowVisible.restype=wintypes.BOOL
    user.GetWindow.argtypes=[wintypes.HWND,wintypes.UINT];user.GetWindow.restype=wintypes.HWND
    enum_callback_type=ctypes.WINFUNCTYPE(wintypes.BOOL,wintypes.HWND,wintypes.LPARAM)
    user.EnumThreadWindows.argtypes=[wintypes.DWORD,enum_callback_type,wintypes.LPARAM]
    startup=StartupInfo();startup.cb=ctypes.sizeof(startup);info=ProcessInfo()
    if args.background:startup.dwFlags|=1;startup.wShowWindow=4 # STARTF_USESHOWWINDOW, SW_SHOWNOACTIVATE
    command=ctypes.create_unicode_buffer(subprocess.list2cmdline([str(program)]))
    if not kernel.CreateProcessW(str(program),command,None,None,False,4,None,str(case),ctypes.byref(startup),ctypes.byref(info)):
        raise ctypes.WinError(ctypes.get_last_error())
    output=case/'presentations.csv';console_path=case/'presentmon.log';monitor=None;samples=[];feature_samples=[];owned_window=None;stopped=False
    monitor_command=[str(MONITOR),'--process_id',str(info.pid),'--output_file',str(output),
        '--v1_metrics','--no_console_stats','--no_track_input','--no_track_gpu','--no_track_display',
        '--session_name','WA2.Codex.'+uuid.uuid4().hex,'--timed',str(args.seconds+2),'--terminate_after_timed']
    started=None;elapsed=None;used=None;memory=None;returncode=None;failure=None
    @enum_callback_type
    def observe_owned_window(window,_):
        nonlocal owned_window
        owner_pid=wintypes.DWORD();user.GetWindowThreadProcessId(window,ctypes.byref(owner_pid))
        if owner_pid.value==info.pid and user.IsWindowVisible(window) and not user.GetWindow(window,4):
            owned_window=window;return False
        return True
    try:
        with console_path.open('wb') as console:
            monitor=subprocess.Popen(monitor_command,stdout=console,stderr=subprocess.STDOUT,creationflags=0x08000000)
            # Start tracing before the suspended original executes its first
            # instruction, so its first Present remains in the trace.
            for _ in range(10):
                if monitor.poll() is not None:raise RuntimeError('PresentMon exited before game start: '+console_path.read_text(errors='replace'))
                time.sleep(.1)
            started=time.perf_counter();started_qpc=qpc()
            if kernel.ResumeThread(info.thread)==0xffffffff:raise ctypes.WinError(ctypes.get_last_error())
            while True:
                fg=user.GetForegroundWindow();pid=wintypes.DWORD()
                if fg:user.GetWindowThreadProcessId(fg,ctypes.byref(pid))
                foreground=pid.value==info.pid
                if foreground:owned_window=fg
                if not owned_window or not user.IsWindow(owned_window):
                    owned_window=None;user.EnumThreadWindows(info.tid,observe_owned_window,0)
                samples.append([qpc(),foreground,bool(user.IsIconic(owned_window)) if owned_window else None,cpu_seconds(info.process)])
                if len(samples)%4==0:feature_samples.append([qpc(),original_crt_features(info.process)])
                if kernel.WaitForSingleObject(info.process,0)==0:break
                if monitor.poll() is not None or time.perf_counter()-started>=args.seconds:break
                time.sleep(.25)
            elapsed=time.perf_counter()-started;used=cpu_seconds(info.process);memory=memory_counters(info.process)
            if kernel.WaitForSingleObject(info.process,0)!=0:
                kernel.TerminateProcess(info.process,0xe0000001);kernel.WaitForSingleObject(info.process,15000);stopped=True
            code=wintypes.DWORD();kernel.GetExitCodeProcess(info.process,ctypes.byref(code));returncode=code.value
            monitor.wait(timeout=20)
    except Exception as error:failure=str(error)
    finally:
        if kernel.WaitForSingleObject(info.process,0)!=0:kernel.TerminateProcess(info.process,0xe0000001);kernel.WaitForSingleObject(info.process,15000)
        kernel.CloseHandle(info.thread);kernel.CloseHandle(info.process)
        if monitor and monitor.poll() is None:monitor.kill();monitor.wait(timeout=15)
    rows=[]
    if output.exists():
        with output.open(newline='',encoding='utf-8-sig') as stream:rows=list(csv.DictReader(stream))
    record=dict(schema='wa2.original-etw-benchmark.v1',input_sha256=digest(program),program_file=str(program),
        requested_window_state='background' if args.background else 'default',
        root=str(case),resource_source=str(source),linked_resources=linked,copied_files=copied,pid=info.pid,
        requested_seconds=args.seconds,elapsed_seconds=round(elapsed,3) if elapsed is not None else None,
        game_started_qpc=started_qpc if started else None,qpc_hz=qpc_hz(),timed_measurement_stop=stopped,host_return_code=returncode,
        presentmon_command=monitor_command,presentmon_sha256=MONITOR_SHA,presentmon_return_code=monitor.returncode if monitor else None,
        csv_file=str(output),csv_sha256=digest(output) if output.exists() else None,present_rows=len(rows),columns=list(rows[0]) if rows else [],
        samples=samples,crt_feature_samples=feature_samples,process_cpu_seconds=used,process_memory=memory,error=failure,
        profile_before=profile_before,profile_after=profile_hashes(),
        screen_access=False,input_tracking=False,automated_input=False,normal_exit_verified=False,gameplay_verified=False,
        limits='Original process only; ETW Present events and owned process counters. No pixels or scene validation. Original native user-profile settings can differ from the compatibility layer private settings.')
    evidence.write_text(json.dumps(record,indent=2)+'\n')
    print(json.dumps({key:record[key] for key in ('pid','elapsed_seconds','timed_measurement_stop','presentmon_return_code','present_rows','columns','process_cpu_seconds','process_memory','error')},indent=2))
    return 0 if not failure and stopped and elapsed>=args.seconds and rows and monitor.returncode==0 else 1


if __name__=='__main__':raise SystemExit(main())
