"""Measure only the original process/window created by this diagnostic."""
from pathlib import Path
import ctypes as C
from ctypes import wintypes as W
import hashlib
import json
import subprocess
import time
import uuid
from PIL import Image,ImageChops,ImageStat
import verify_normal_launch as probe

def main():
    root=probe.ROOT;case=root/'build/original-startup'/uuid.uuid4().hex;case.mkdir(parents=True)
    reference=Image.open(root/'logs/normal-launch-menu.png').convert('RGB').crop((270,160,1020,255))
    program=root/'WA2_chs.exe';started=time.perf_counter();process=subprocess.Popen([str(program)],cwd=root/'res',creationflags=subprocess.CREATE_NO_WINDOW)
    observations=[];first_window=None;first_content=None;menu=None;closed=False;close_time=0;confirmed=False;forced=False
    try:
        while process.poll() is None and time.perf_counter()-started<35:
            windows=[];dialogs=[]
            @probe.CB
            def visit(hwnd,_):
                pid=W.DWORD();probe.U.GetWindowThreadProcessId(hwnd,C.byref(pid))
                if pid.value==process.pid:
                    info=probe.window_info(hwnd)
                    if info['visible'] and info['class']=='#32770':dialogs.append(info)
                    if info['visible'] and info['class']=='WHITE ALBUM2CN':windows.append(info)
                return True
            probe.U.EnumWindows(visit,0)
            windows.sort(key=lambda item:item['client'][2]*item['client'][3],reverse=True)
            now=time.perf_counter()-started
            if windows:
                window=windows[0]
                if first_window is None:first_window=now
                if window['client'][2]>0 and window['client'][3]>0 and not closed:
                    capture=probe.capture(window['hwnd'],case/'latest.bmp')
                    if 'path' in capture:
                        picture=Image.open(capture['path']).convert('RGB');normalized=picture.resize((1280,720))
                        difference=sum(ImageStat.Stat(ImageChops.difference(normalized.crop((270,160,1020,255)),reference)).mean)/3
                        if first_content is None and capture['nonblack_sample_ratio']>0.02:first_content=now
                        observations.append({'seconds':round(now,4),'window':window,'nonblack':capture['nonblack_sample_ratio'],'menu_template_mean_error':round(difference,3)})
                        if difference<20:
                            menu=now;picture.save(case/'menu.png');probe.U.PostMessageW(window['hwnd'],0x10,0,0);closed=True;close_time=time.perf_counter()
            for dialog in dialogs:
                controls=[]
                @probe.CB
                def control(hwnd,_):
                    item=probe.window_info(hwnd);item['id']=probe.U.GetDlgCtrlID(hwnd);controls.append(item);return True
                probe.U.EnumChildWindows(dialog['hwnd'],control,0)
                (case/'dialog.json').write_text(json.dumps({'dialog':dialog,'controls':controls},indent=2))
                text=' '.join([dialog['title'],*[item['title'] for item in controls]])
                yes=next((item for item in controls if item['id']==6),None)
                if closed and not confirmed and yes and any(word in text for word in ['结束','退出','終了']):
                    result=C.c_size_t();probe.U.SendMessageTimeoutW(yes['hwnd'],0xf5,0,0,0x22,2000,C.byref(result));confirmed=True
            if closed and time.perf_counter()-close_time>10:break
            (case/'observations.json').write_text(json.dumps(observations,indent=2)+'\n')
            time.sleep(.05)
    finally:
        if process.poll() is None:forced=True;process.terminate()
        process.wait(timeout=10)
    result={'schema':'wa2.original-startup.v1','program':str(program),'input_sha256':hashlib.sha256(program.read_bytes()).hexdigest(),
            'case':str(case),'first_visible_window_seconds':first_window,'first_visible_content_seconds':first_content,
            'menu_template_match_seconds':menu,'elapsed_seconds':time.perf_counter()-started,'exit_code':process.returncode,
            'forced_diagnostic_cleanup':forced,'exit_confirmed':confirmed,
            'settings_caveat':'Original game uses its native settings locations; recompilation uses the resource-root virtual settings. This is not a proven identical-settings comparison.'}
    (root/'logs/original-startup.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2),flush=True)

if __name__=='__main__':main()
