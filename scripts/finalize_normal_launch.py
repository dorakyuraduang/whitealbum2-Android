"""Bind a visually inspected native-window launch to the exact tested binary."""
from pathlib import Path
import argparse
import hashlib
import json
from PIL import Image

ROOT=Path(__file__).resolve().parent.parent

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--menu-shot',type=Path,required=True)
    parser.add_argument('--menu-inspected',action='store_true',required=True)
    parser.add_argument('--standalone',action='store_true')
    args=parser.parse_args()
    source=ROOT/'logs'/('standalone-launch-after.json' if args.standalone else 'normal-launch-after.json')
    launch=json.loads(source.read_text())
    expected_mode='standalone_resource_executable' if args.standalone else 'ordinary_launcher'
    if launch.get('launch_mode','ordinary_launcher')!=expected_mode:raise ValueError('Unexpected launch mode')
    if args.standalone and len(launch['command'])!=1:raise ValueError('Standalone proof must launch without arguments')
    if any(option in launch['command'] for option in ['--profile','--frames','--snapshot']):raise ValueError('Ordinary-launch proof cannot use diagnostic runtime switches')
    output=ROOT/'out/WA2_SDL2_x64.exe'
    if launch['output_sha256']!=digest(output):raise ValueError('Tested output no longer matches')
    case=Path(launch['case'])
    runtime=case/'runtime.json';report=json.loads(runtime.read_text(encoding='utf-8-sig'))
    state=report.get('process',report)
    if report.get('error') or launch['launcher_return_code']!=0 or launch['forced_diagnostic_cleanup']:
        raise ValueError('Ordinary launch did not end successfully')
    if not launch['close_requested'] or not launch['exit_confirmed'] or not state['exited'] or state['exit_code']!=0:
        raise ValueError('Normal confirmed exit not verified')
    if state['diagnostic_stop'] or state['direct3d9']['frames_captured']:
        raise ValueError('This was a diagnostic rendering path')
    observation=next(item for item in launch['observations']
                     if Path(item['client_capture'].get('path','')).resolve()==args.menu_shot.resolve())
    client=observation['client_capture']
    if not observation['root']['visible'] or not client['bitblt_success'] or min(client['width'],client['height'])<=0:
        raise ValueError('Invalid native window capture')
    if any(child['class']=='VideoRenderer' and child['visible'] for child in observation['children']):
        raise ValueError('Video window still overlays the menu')
    screenshot=ROOT/'logs'/('standalone-launch-menu.png' if args.standalone else 'normal-launch-menu.png')
    Image.open(args.menu_shot).save(screenshot)
    proof={
        'schema':'wa2.normal-launch-verification.v1',
        'output_sha256':launch['output_sha256'],
        'command':launch['command'],
        'runtime_options':launch['runtime_options'],
        'launch_mode':expected_mode,'native_console':launch.get('native_console',False),'startup_timing':launch.get('startup_timing'),
        'resource_root':str(ROOT/'res'),
        'host_return_code':launch['launcher_return_code'],
        'normal_exit_verified':True,
        'menu_visually_inspected':args.menu_inspected,
        'menu_observation':observation,
        'menu_screenshot':{'file':str(screenshot),'sha256':digest(screenshot)},
        'runtime_report':{'file':str(runtime),'sha256':digest(runtime)},
        'launch_report':{'file':str(source),'sha256':digest(source)},
        'presented_frames':state['direct3d9']['presents'],
        'offscreen_frames_captured':state['direct3d9']['frames_captured'],
        'directshow_final':state['directshow'],
        'save_before_sha256':launch['save_before_sha256'],
        'save_after_sha256':launch['save_after_sha256'],
        'scope':('Bare EXE in resource directory, unrelated working directory, no arguments' if args.standalone else 'Ordinary run.cmd launcher')+', actual visible client/title menu, video overlay release and confirmed normal exit',
        'gameplay_verified':False,
        'original_startup_speed_matched':False,
        'regressions':{
            'client_area_before':'logs/graphics-zero-client-reproduction.log',
            'client_area_after':'logs/graphics-tests.log',
            'video_overlay_before':'logs/media-overlay-reproduction.log',
            'video_overlay_after':'logs/media-tests.log',
            'optional_module_before':'logs/optional-dll-reproduction.log',
            'runtime_after':'logs/normal-window-runtime-tests.log',
        },
    }
    destination=ROOT/'logs'/('standalone-launch-verification.json' if args.standalone else 'normal-launch-verification.json')
    destination.write_text(json.dumps(proof,indent=2)+'\n')
    print(json.dumps({'proof':str(destination),'output_sha256':proof['output_sha256'],
                      'menu':str(screenshot),'normal_exit_verified':True},indent=2))

if __name__=='__main__':main()
