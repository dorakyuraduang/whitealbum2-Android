"""Inspect the real run.cmd path without changing the game's runtime options."""
from pathlib import Path
import argparse
import ctypes as C
from ctypes import wintypes as W
import hashlib
import json
import os
import shutil
import struct
import subprocess
import time
import uuid
from PIL import Image,ImageChops,ImageStat

ROOT=Path(__file__).resolve().parent.parent
U=C.WinDLL('user32',use_last_error=True)
G=C.WinDLL('gdi32',use_last_error=True)
K=C.WinDLL('kernel32',use_last_error=True)
CB=C.WINFUNCTYPE(W.BOOL,W.HWND,W.LPARAM)
U.EnumWindows.argtypes=[CB,W.LPARAM]
U.EnumChildWindows.argtypes=[W.HWND,CB,W.LPARAM]
U.GetWindowThreadProcessId.argtypes=[W.HWND,C.POINTER(W.DWORD)]
U.GetClassNameW.argtypes=[W.HWND,W.LPWSTR,C.c_int]
U.GetWindowTextW.argtypes=[W.HWND,W.LPWSTR,C.c_int]
U.GetClientRect.argtypes=[W.HWND,C.POINTER(W.RECT)]
U.GetWindowRect.argtypes=[W.HWND,C.POINTER(W.RECT)]
U.GetWindowLongPtrW.argtypes=[W.HWND,C.c_int]
U.GetWindowLongPtrW.restype=C.c_ssize_t
U.IsWindowVisible.argtypes=[W.HWND]
U.IsIconic.argtypes=[W.HWND]
U.PostMessageW.argtypes=[W.HWND,W.UINT,W.WPARAM,W.LPARAM]
U.GetForegroundWindow.restype=W.HWND
U.SetForegroundWindow.argtypes=[W.HWND]
U.ShowWindow.argtypes=[W.HWND,C.c_int]
U.BringWindowToTop.argtypes=[W.HWND]
U.AttachThreadInput.argtypes=[W.DWORD,W.DWORD,W.BOOL]
K.GetCurrentThreadId.restype=W.DWORD
U.GetDlgCtrlID.argtypes=[W.HWND]
U.SendMessageTimeoutW.argtypes=[W.HWND,W.UINT,C.c_size_t,C.c_ssize_t,W.UINT,W.UINT,C.POINTER(C.c_size_t)]
U.SendMessageTimeoutW.restype=C.c_ssize_t
U.GetDC.argtypes=[W.HWND]
U.GetDC.restype=W.HDC
U.ReleaseDC.argtypes=[W.HWND,W.HDC]
G.CreateCompatibleDC.argtypes=[W.HDC]
G.CreateCompatibleDC.restype=W.HDC
G.CreateDIBSection.argtypes=[W.HDC,C.c_void_p,W.UINT,C.POINTER(C.c_void_p),W.HANDLE,W.DWORD]
G.CreateDIBSection.restype=W.HBITMAP
G.SelectObject.argtypes=[W.HDC,W.HANDLE]
G.SelectObject.restype=W.HANDLE
G.BitBlt.argtypes=[W.HDC,C.c_int,C.c_int,C.c_int,C.c_int,W.HDC,C.c_int,C.c_int,W.DWORD]
G.DeleteObject.argtypes=[W.HANDLE]
G.DeleteDC.argtypes=[W.HDC]
K.CreateToolhelp32Snapshot.argtypes=[W.DWORD,W.DWORD]
K.CreateToolhelp32Snapshot.restype=W.HANDLE
K.CloseHandle.argtypes=[W.HANDLE]
K.OpenProcess.argtypes=[W.DWORD,W.BOOL,W.DWORD]
K.OpenProcess.restype=W.HANDLE
K.QueryFullProcessImageNameW.argtypes=[W.HANDLE,W.DWORD,W.LPWSTR,C.POINTER(W.DWORD)]
K.TerminateProcess.argtypes=[W.HANDLE,W.UINT]
K.WaitForSingleObject.argtypes=[W.HANDLE,W.DWORD]
class PE(C.Structure):
    _fields_=[('size',W.DWORD),('uses',W.DWORD),('pid',W.DWORD),('heap',C.c_size_t),
              ('module',W.DWORD),('threads',W.DWORD),('parent',W.DWORD),('priority',W.LONG),
              ('flags',W.DWORD),('name',W.WCHAR*260)]
K.Process32FirstW.argtypes=[W.HANDLE,C.POINTER(PE)]
K.Process32NextW.argtypes=[W.HANDLE,C.POINTER(PE)]

def descendants(pid):
    snapshot=K.CreateToolhelp32Snapshot(2,0)
    if snapshot==C.c_void_p(-1).value:raise C.WinError(C.get_last_error())
    entries=[]
    try:
        entry=PE();entry.size=C.sizeof(entry)
        more=K.Process32FirstW(snapshot,C.byref(entry))
        while more:
            entries.append((entry.pid,entry.parent,entry.name))
            more=K.Process32NextW(snapshot,C.byref(entry))
    finally:K.CloseHandle(snapshot)
    owned={pid}
    for _ in range(8):
        next_owned=owned|{child for child,parent,_ in entries if parent in owned}
        if owned==next_owned:break
        owned=next_owned
    return {child:name for child,_,name in entries if child in owned}

def window_info(hwnd):
    pid=W.DWORD();U.GetWindowThreadProcessId(hwnd,C.byref(pid))
    name=C.create_unicode_buffer(256);title=C.create_unicode_buffer(256)
    U.GetClassNameW(hwnd,name,len(name));U.GetWindowTextW(hwnd,title,len(title))
    rect=W.RECT();U.GetWindowRect(hwnd,C.byref(rect))
    client=W.RECT();C.set_last_error(0);client_ok=U.GetClientRect(hwnd,C.byref(client));client_error=C.get_last_error()
    return {'hwnd':int(hwnd),'pid':pid.value,'class':name.value,'title':title.value,
            'visible':bool(U.IsWindowVisible(hwnd)),'minimized':bool(U.IsIconic(hwnd)),'style':hex(U.GetWindowLongPtrW(hwnd,-16)&0xffffffff),
            'rect':[rect.left,rect.top,rect.right,rect.bottom],
            'client':[client.left,client.top,client.right,client.bottom],'client_ok':bool(client_ok),'client_error':client_error}

def capture(hwnd,path):
    rect=W.RECT();U.GetClientRect(hwnd,C.byref(rect));width,height=rect.right,rect.bottom
    if not (0<width<=7680 and 0<height<=4320):return {'error':'invalid client dimensions','width':width,'height':height}
    # Read this game's client DC, never the desktop or another application's DC.
    source=U.GetDC(hwnd);dest=G.CreateCompatibleDC(source);bits=C.c_void_p()
    header=struct.pack('<IiiHHIIiiII',40,width,-height,1,32,0,width*height*4,0,0,0,0)
    info=C.create_string_buffer(header)
    bitmap=G.CreateDIBSection(source,info,0,C.byref(bits),None,0)
    if not bitmap:raise C.WinError(C.get_last_error())
    old=G.SelectObject(dest,bitmap)
    try:
        ok=G.BitBlt(dest,0,0,width,height,source,0,0,0x40cc0020)
        G.GdiFlush()
        pixels=C.string_at(bits,width*height*4)
    finally:
        G.SelectObject(dest,old);G.DeleteObject(bitmap);G.DeleteDC(dest);U.ReleaseDC(hwnd,source)
    path.write_bytes(struct.pack('<2sIHHI',b'BM',54+len(pixels),0,0,54)+header+pixels)
    sampled=range(0,len(pixels),64)
    nonblack=sum(any(pixels[i:i+3]) for i in sampled)
    return {'path':str(path),'bitblt_success':bool(ok),'width':width,'height':height,
            'nonblack_sample_ratio':nonblack/len(sampled),'sha256':hashlib.sha256(pixels).hexdigest()}

def focus_game(hwnd):
    """One explicit test phase transition; never repeatedly steals focus."""
    own=K.GetCurrentThreadId();foreground=U.GetForegroundWindow()
    foreground_thread=U.GetWindowThreadProcessId(foreground,None) if foreground else 0
    game_thread=U.GetWindowThreadProcessId(hwnd,None)
    attached=[]
    try:
        for thread in {foreground_thread,game_thread}-{0,own}:
            if U.AttachThreadInput(own,thread,True):attached.append(thread)
        if U.IsIconic(hwnd):U.ShowWindow(hwnd,9)
        U.BringWindowToTop(hwnd);accepted=bool(U.SetForegroundWindow(hwnd))
        return {'set_foreground_accepted':accepted,'foreground_hwnd':U.GetForegroundWindow(),'target_hwnd':hwnd,
                'attached_threads':len(attached),'minimized':bool(U.IsIconic(hwnd))}
    finally:
        for thread in reversed(attached):U.AttachThreadInput(own,thread,False)

class ShellProcess:
    """Launch the resource EXE through the same Windows shell verb as Explorer."""
    class Info(C.Structure):
        _fields_=[('cbSize',W.DWORD),('fMask',W.ULONG),('hwnd',W.HWND),('lpVerb',W.LPCWSTR),
                  ('lpFile',W.LPCWSTR),('lpParameters',W.LPCWSTR),('lpDirectory',W.LPCWSTR),
                  ('nShow',C.c_int),('hInstApp',W.HINSTANCE),('lpIDList',C.c_void_p),
                  ('lpClass',W.LPCWSTR),('hkeyClass',W.HKEY),('dwHotKey',W.DWORD),
                  ('hIcon',W.HANDLE),('hProcess',W.HANDLE)]
    def __init__(self,program,directory):
        shell=C.WinDLL('shell32',use_last_error=True)
        shell.ShellExecuteExW.argtypes=[C.POINTER(self.Info)];shell.ShellExecuteExW.restype=W.BOOL
        K.GetProcessId.argtypes=[W.HANDLE];K.GetProcessId.restype=W.DWORD
        K.GetExitCodeProcess.argtypes=[W.HANDLE,C.POINTER(W.DWORD)]
        self.command=[str(program)];self.returncode=None
        info=self.Info();info.cbSize=C.sizeof(info);info.fMask=0x40|0x400
        info.lpVerb='open';info.lpFile=str(program);info.lpDirectory=str(directory);info.nShow=1
        if not shell.ShellExecuteExW(C.byref(info)):raise C.WinError(C.get_last_error())
        if not info.hProcess:raise RuntimeError('Shell did not return an owned process handle')
        self.handle=info.hProcess;self.pid=K.GetProcessId(self.handle)
        if not self.pid:raise C.WinError(C.get_last_error())
    def poll(self):
        if self.returncode is not None:return self.returncode
        state=K.WaitForSingleObject(self.handle,0)
        if state==258:return None
        if state!=0:raise C.WinError(C.get_last_error())
        value=W.DWORD()
        if not K.GetExitCodeProcess(self.handle,C.byref(value)):raise C.WinError(C.get_last_error())
        self.returncode=value.value;return self.returncode
    def wait(self,timeout):
        state=K.WaitForSingleObject(self.handle,int(timeout*1000))
        if state==258:raise subprocess.TimeoutExpired(self.command,timeout)
        if state!=0:raise C.WinError(C.get_last_error())
        return self.poll()
    def kill(self):
        if self.poll() is None and not K.TerminateProcess(self.handle,124):raise C.WinError(C.get_last_error())
    def close(self):
        if self.handle:K.CloseHandle(self.handle);self.handle=None

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--profile',action='store_true');parser.add_argument('--auto-close-menu',action='store_true');parser.add_argument('--standalone',action='store_true');parser.add_argument('--shell-launch',action='store_true');parser.add_argument('--native-console',action='store_true');parser.add_argument('--program',type=Path);parser.add_argument('--menu-observe-seconds',type=float,default=0);parser.add_argument('--focus-menu',action='store_true');parser.add_argument('--foreground-phase-at',type=float);args=parser.parse_args()
    if args.standalone and args.program:parser.error('--standalone and --program are mutually exclusive')
    if args.shell_launch and (not args.standalone or args.native_console or args.profile):parser.error('--shell-launch requires a bare standalone launch')
    case=ROOT/'build/normal-startup'/uuid.uuid4().hex
    case.mkdir(parents=True)
    save=ROOT/'res/Documents/Leaf/WHITE ALBUM2/Sys.sav'
    shutil.copy2(save,case/'Sys.sav.before')
    for name in ['runtime.json','launch-console.log']:
        path=ROOT/'logs'/name
        if path.exists():shutil.copy2(path,case/('before-'+name))
    program=(ROOT/'res/WA2_SDL2_x64.exe' if args.standalone else args.program or ROOT/'out/WA2_SDL2_x64.exe').resolve()
    output_sha256=hashlib.sha256(program.read_bytes()).hexdigest()
    console=(case/'launcher-console.log').open('wb')
    command=[os.environ['COMSPEC'],'/d','/c',str(ROOT/'run.cmd'),'--no-pause']
    report_path=ROOT/'logs/runtime.json'
    mode='ordinary_launcher'
    if args.standalone:
        command=[str(program)];report_path=ROOT/'res/wa2-runtime.json';mode='standalone_resource_executable'
    elif args.program:
        report_path=case/'runtime.json';mode='comparison_program'
        command=[str(program),str(ROOT/'WA2_chs.exe'),'--root',str(ROOT/'res'),'--budget','1000000000000','--report',str(report_path),'--allow-write']
    if args.profile:
        report_path=case/'runtime.json'
        command=[str(program),str(ROOT/'WA2_chs.exe'),'--root',str(ROOT/'res'),'--budget','1000000000000','--report',str(report_path),'--allow-write','--profile'];mode='profile'
    if report_path.exists():report_path.unlink()
    startup=None
    if args.native_console:
        startup=subprocess.STARTUPINFO();startup.dwFlags=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
    process=ShellProcess(program,case) if args.shell_launch else subprocess.Popen(command,cwd=case if args.standalone else ROOT,
                             stdout=None if args.native_console else console,stderr=None if args.native_console else subprocess.STDOUT,
                             startupinfo=startup,creationflags=subprocess.CREATE_NEW_CONSOLE if args.native_console else subprocess.CREATE_NO_WINDOW)
    started=time.monotonic();shots=[];closed=False;close_time=0;game_handle=None;game_pid=None
    foreground_attempted=False;dialogs=[];confirmation_sent=False
    last_report={};known=set()
    first_window=None;first_content=None;menu_time=None;last_timing_capture=0;menu_candidate=0
    steady_samples=[];steady_captures=set();last_steady_sample=0;menu_focus_attempted=False
    presentation_times={};presentation_clock_hz=None
    focus_transitions=[];foreground_phase_started=False
    # Use the original game's independently captured logo. A port screenshot
    # can contain the same defect under test and must not define correctness.
    reference_path=ROOT/'build/original-startup/a35401246fe04e969c4604d7cdb7756c/menu.png'
    reference=Image.open(reference_path).convert('RGB').crop((270,160,1020,255)) if args.auto_close_menu else None
    try:
        while process.poll() is None and time.monotonic()-started<360:
            owned=descendants(process.pid)
            roots=[]
            modal=[]
            @CB
            def root_window(hwnd,_):
                pid=W.DWORD();U.GetWindowThreadProcessId(hwnd,C.byref(pid))
                if pid.value in owned:
                    info=window_info(hwnd)
                    if info['class'].startswith('WinRecomp.') and info['class'].endswith('.WHITE ALBUM2CN'):roots.append(info)
                    if info['class']=='#32770' and info['visible']:modal.append(info)
                return True
            U.EnumWindows(root_window,0)
            # The game also creates a hidden zero-height "menuWnd" with the
            # same class. Inspect the visible game window, not the first HWND.
            roots.sort(key=lambda row:(row['visible'],(row['rect'][2]-row['rect'][0])*(row['rect'][3]-row['rect'][1])),reverse=True)
            if roots:(case/'windows-latest.json').write_text(json.dumps(roots,indent=2)+'\n')
            if roots and roots[0]['visible'] and not foreground_attempted:
                U.SetForegroundWindow(roots[0]['hwnd']);foreground_attempted=True
            if roots and game_handle is None:
                game_pid=roots[0]['pid'];candidate=K.OpenProcess(0x101001,False,game_pid)
                path=C.create_unicode_buffer(32768);length=W.DWORD(len(path))
                if not candidate or not K.QueryFullProcessImageNameW(candidate,0,path,C.byref(length)):
                    raise C.WinError(C.get_last_error())
                if Path(path.value).resolve()!=program:
                    K.CloseHandle(candidate);raise RuntimeError('Unexpected launcher descendant executable')
                game_handle=candidate
            elapsed=time.monotonic()-started
            if roots and roots[0]['visible'] and first_window is None:first_window=elapsed
            try:
                last_report=json.loads(report_path.read_text(encoding='utf-8-sig'))
                state=last_report.get('process',last_report)
                frames=state.get('direct3d9',{}).get('presents',0) if state else 0
            except (FileNotFoundError,json.JSONDecodeError,PermissionError):frames=0
            for threshold in [1,3,100,200,250,300,400,500]:
                if frames<threshold or threshold in known or not roots:continue
                children=[]
                @CB
                def child_window(hwnd,_):children.append(window_info(hwnd));return True
                U.EnumChildWindows(roots[0]['hwnd'],child_window,0)
                shot={'elapsed_seconds':round(time.monotonic()-started,3),'frames':frames,
                      'root':roots[0],'children':children,
                      'client_capture':capture(roots[0]['hwnd'],case/f'client-{threshold}.bmp')}
                shots.append(shot);known.add(threshold)
                (case/f'runtime-at-{threshold}.json').write_text(json.dumps(last_report,indent=2)+'\n')
                (case/'observations.json').write_text(json.dumps(shots,indent=2)+'\n')
                print(json.dumps({'frames':frames,'children':children,'capture':shot['client_capture']}),flush=True)
            if reference is not None and menu_time is None and not closed and roots and roots[0]['visible'] and elapsed-last_timing_capture>=.15:
                last_timing_capture=elapsed;picture_info=capture(roots[0]['hwnd'],case/'timing-latest.bmp')
                if 'path' in picture_info:
                    picture=Image.open(picture_info['path']).convert('RGB');normalized=picture.resize((1280,720))
                    error=sum(ImageStat.Stat(ImageChops.difference(normalized.crop((270,160,1020,255)),reference)).mean)/3
                    if first_content is None and picture_info['nonblack_sample_ratio']>.02:first_content=elapsed
                    if menu_time is None and error<20:
                        if not menu_candidate:menu_candidate=elapsed
                        elif elapsed-menu_candidate>=.15:
                            menu_time=menu_candidate;menu_path=case/'client-menu.bmp';shutil.copy2(case/'timing-latest.bmp',menu_path)
                            children=[]
                            @CB
                            def menu_child(hwnd,_):children.append(window_info(hwnd));return True
                            U.EnumChildWindows(roots[0]['hwnd'],menu_child,0)
                            shot={'elapsed_seconds':round(elapsed,4),'frames':frames,'root':roots[0],'children':children,'client_capture':dict(picture_info,path=str(menu_path)),'menu_template_mean_error':error}
                            shots.append(shot);(case/'observations.json').write_text(json.dumps(shots,indent=2)+'\n')
                            (case/'runtime-at-menu.json').write_text(json.dumps(last_report,indent=2)+'\n')
                            if not args.menu_observe_seconds:(case/'close-request').write_text('Menu template matched; capture retained for visual review.\n')
                            print(json.dumps({'menu_template_match_seconds':menu_time,'capture':shot['client_capture']}),flush=True)
                    elif menu_time is None:menu_candidate=0
                (case/'startup-timing.json').write_text(json.dumps({'first_visible_window_seconds':first_window,'first_visible_content_seconds':first_content,'menu_template_match_seconds':menu_time},indent=2)+'\n')
            if menu_time is not None and args.menu_observe_seconds and not closed:
                age=elapsed-menu_time
                if args.focus_menu and not menu_focus_attempted and roots:
                    menu_focus_attempted=True
                    focus_transitions.append(dict(focus_game(roots[0]['hwnd']),seconds_after_menu=age))
                if args.foreground_phase_at is not None and age>=args.foreground_phase_at and not foreground_phase_started and roots:
                    foreground_phase_started=True
                    focus_transitions.append(dict(focus_game(roots[0]['hwnd']),seconds_after_menu=age))
                (case/'focus-transitions.json').write_text(json.dumps(focus_transitions,indent=2)+'\n')
                if elapsed-last_steady_sample>=.5:
                    last_steady_sample=elapsed
                    graphics=state.get('direct3d9',{})
                    presentation_clock_hz=graphics.get('presentation_clock_hz')
                    presentation_times.update({row[0]:row for row in graphics.get('recent_presentations',[])})
                    active=U.GetForegroundWindow();active_pid=W.DWORD()
                    if active:U.GetWindowThreadProcessId(active,C.byref(active_pid))
                    steady_samples.append({'seconds_after_menu':round(age,4),'elapsed_seconds':round(elapsed,4),'presents':frames,'native_instructions':state.get('native_instructions',0),'api_calls':state.get('api_calls',0),'draw_calls':graphics.get('draw_calls',0),'workers':state.get('threads',{}).get('workers',[]),'foreground_game':active_pid.value==game_pid,'window':roots[0] if roots else None})
                    (case/'steady-samples.json').write_text(json.dumps(steady_samples,indent=2)+'\n')
                for threshold in [5,15,30,45,60]:
                    if age<threshold or threshold in steady_captures or not roots:continue
                    path=case/f'client-menu-plus-{threshold}.bmp';captured=capture(roots[0]['hwnd'],path);steady_captures.add(threshold)
                    (case/f'runtime-menu-plus-{threshold}.json').write_text(json.dumps(last_report,indent=2)+'\n')
                    print(json.dumps({'menu_observation_seconds':threshold,'frames':frames,'capture':captured}),flush=True)
                if age>=args.menu_observe_seconds:(case/'close-request').write_text('Sustained menu observation complete.\n')
            for dialog in modal:
                controls=[]
                @CB
                def control(hwnd,_):
                    item=window_info(hwnd);item['id']=U.GetDlgCtrlID(hwnd);controls.append(item);return True
                U.EnumChildWindows(dialog['hwnd'],control,0)
                item={'dialog':dialog,'controls':controls,'elapsed_seconds':round(time.monotonic()-started,3)}
                if not dialogs or dialogs[-1]['dialog']['hwnd']!=dialog['hwnd']:dialogs.append(item)
                text=' '.join([dialog['title'],*[c['title'] for c in controls]])
                yes=next((c for c in controls if c['id']==6),None)
                if closed and not confirmation_sent and yes and any(word in text.lower() for word in ['退出','结束','終了','quit','exit']):
                    result=C.c_size_t();U.SendMessageTimeoutW(yes['hwnd'],0xf5,0,0,0x22,2000,C.byref(result));confirmation_sent=True
                (case/'dialogs.json').write_text(json.dumps(dialogs,indent=2)+'\n')
            if not closed and (case/'close-request').exists() and roots:
                U.PostMessageW(roots[0]['hwnd'],0x10,0,0);closed=True;close_time=time.monotonic()
            if closed and time.monotonic()-close_time>60:break
            time.sleep(.05 if reference is not None else .5)
    finally:
        forced=False
        if game_handle:
            if K.WaitForSingleObject(game_handle,0)==258:
                forced=True;K.TerminateProcess(game_handle,124);K.WaitForSingleObject(game_handle,10000)
            K.CloseHandle(game_handle)
        try:process.wait(timeout=10)
        except subprocess.TimeoutExpired:process.kill();process.wait(timeout=10)
        console.close()
        if args.shell_launch:process.close()
    if report_path.exists() and report_path.resolve()!=(case/'runtime.json').resolve():shutil.copy2(report_path,case/'runtime.json')
    (case/'presentation-times.json').write_text(json.dumps({'clock_hz':presentation_clock_hz,'frames':list(presentation_times.values())},indent=2)+'\n')
    if (ROOT/'logs/launch-console.log').exists() and mode=='ordinary_launcher':shutil.copy2(ROOT/'logs/launch-console.log',case/'launch-console.log')
    result={'schema':'wa2.normal-launch.v1','command':command,'case':str(case),'game_pid':game_pid,
            'output_sha256':output_sha256,
            'launch_mode':mode,'working_directory':str(case if args.standalone else ROOT),
            'launch_method':'ShellExecuteEx(open)' if args.shell_launch else 'CreateProcess',
            'menu_observation_seconds':args.menu_observe_seconds,'steady_samples':steady_samples,
            'presentation_clock_hz':presentation_clock_hz,'presentation_times':list(presentation_times.values()),
            'focus_transitions':focus_transitions,
            'menu_reference':{'file':str(reference_path),'sha256':hashlib.sha256(reference_path.read_bytes()).hexdigest()},
            'native_console':args.native_console,
            'startup_timing':{'first_visible_window_seconds':first_window,'first_visible_content_seconds':first_content,'menu_template_match_seconds':menu_time,'criterion':'Visible client DC; title logo crop mean error < 20 for two samples. No input or movie skipping.'},
            'runtime_options':{'profile':'Native executable with --profile; no --frames or --snapshot','standalone_resource_executable':'Resource-directory native GUI EXE, no arguments','comparison_program':'Comparison executable with explicit image/root; no --frames, --snapshot or --profile','ordinary_launcher':'run.cmd; no --frames, --snapshot, or --profile'}[mode],
            'elapsed_seconds':round(time.monotonic()-started,3),'observations':shots,
            'close_requested':closed,'forced_diagnostic_cleanup':forced,'launcher_return_code':process.returncode,
            'exit_dialogs':dialogs,'exit_confirmed':confirmation_sent,
            'save_before_sha256':hashlib.sha256((case/'Sys.sav.before').read_bytes()).hexdigest(),
            'save_after_sha256':hashlib.sha256(save.read_bytes()).hexdigest(),'last_report':last_report}
    result_path=ROOT/'logs'/({'profile':'profile-launch-after.json','comparison_program':'comparison-launch-after.json','standalone_resource_executable':'standalone-launch-after.json'}.get(mode,'normal-launch-after.json'))
    result_path.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({key:result[key] for key in ['case','elapsed_seconds','close_requested','forced_diagnostic_cleanup','launcher_return_code']}),flush=True)

if __name__=='__main__':main()
