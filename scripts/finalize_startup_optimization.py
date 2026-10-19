"""Bind actual-window timing and bridge regressions to the delivered build."""
from pathlib import Path
import hashlib
import json

ROOT=Path(__file__).resolve().parent.parent
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def read(name):return json.loads((ROOT/'logs'/name).read_text(encoding='utf-8-sig'))
def reference(path):return {'file':str(path),'sha256':digest(path)}

def main():
    output=ROOT/'out/WA2_SDL2_x64.exe';sha=digest(output)
    after=read('normal-launch-after.json');standalone=read('standalone-launch-after.json');before=read('comparison-launch-after.json');original=read('original-startup.json')
    for launch in [after,standalone,before]:
        assert launch['launcher_return_code']==0 and launch['close_requested'] and launch['exit_confirmed'] and not launch['forced_diagnostic_cleanup']
        assert launch['startup_timing']['menu_template_match_seconds'] is not None
        assert launch['save_before_sha256']==launch['save_after_sha256']
        assert all(flag not in launch['command'] for flag in ['--frames','--profile','--snapshot'])
    for launch in [after,standalone]:assert launch['output_sha256']==sha
    assert after['native_console'] and after['launch_mode']=='ordinary_launcher'
    assert standalone['launch_mode']=='standalone_resource_executable' and len(standalone['command'])==1
    assert original['exit_code']==0 and original['exit_confirmed'] and not original['forced_diagnostic_cleanup']
    for proof in [read('normal-launch-verification.json'),read('standalone-launch-verification.json')]:
        assert proof['output_sha256']==sha and proof['menu_visually_inspected'] and proof['normal_exit_verified']
    assert read('bridge-suite.json')['output_sha256']==sha and read('embedded-launch-tests.json')['output_sha256']==sha
    assert digest(ROOT/'res/WA2_SDL2_x64.exe')==sha
    previous=before['startup_timing']['menu_template_match_seconds'];current=after['startup_timing']['menu_template_match_seconds']
    # This earlier profile was recorded before the console-mode-only change.
    # Its byte counts and API times describe the same graphics implementation,
    # but are deliberately labelled with that earlier executable's own hash.
    profile_path=ROOT/'build/startup-optimization/crop-profile.json';profile=json.loads(profile_path.read_text())
    old_profile_path=ROOT/'build/startup-optimization/copy-profile.json';old_profile=json.loads(old_profile_path.read_text())
    find_time=lambda report:next(item['ms'] for item in report['wall_profile']['api_inclusive_ms'] if item['api']=='d3dx9_41.dll!D3DXLoadSurfaceFromMemory')
    assert sum(item['calls'] for item in profile['direct3d9']['surface_load_profile'])==sum(item['calls'] for item in old_profile['direct3d9']['surface_load_profile'])
    native_save=Path.home()/'Documents/Leaf/WHITE ALBUM2/Sys.sav';virtual_save=ROOT/'res/Documents/Leaf/WHITE ALBUM2/Sys.sav'
    result={'schema':'wa2.startup-optimization.v1','output_sha256':sha,'input_sha256':digest(ROOT/'WA2_chs.exe'),
            'menu_elapsed_before_seconds':previous,'menu_elapsed_after_seconds':current,'menu_elapsed_reduction_percent':100*(1-current/previous),'menu_elapsed_speedup':previous/current,
            'before':{'output_sha256':before['output_sha256'],'timing':before['startup_timing'],'launch_report':reference(ROOT/'logs/comparison-launch-after.json')},
            'after':{'output_sha256':sha,'timing':after['startup_timing'],'launch_report':reference(ROOT/'logs/normal-launch-after.json'),'native_console':True},
            'standalone':{'output_sha256':sha,'timing':standalone['startup_timing'],'launch_report':reference(ROOT/'logs/standalone-launch-after.json'),'arguments':[],'unrelated_working_directory':True},
            'original':original,'original_startup_speed_matched':False,'no_movie_skip_input_sent':True,'single_run_comparison':True,
            'native_and_virtual_sys_save_equal':digest(native_save)==digest(virtual_save),'sys_save_sha256':digest(virtual_save),'identical_settings_proven':False,
            'remaining_menu_gap_vs_original_seconds':current-original['menu_template_match_seconds'],
            'graphics_load_profile':{'scope':'Equal 1234 source-rectangle loads in 30-present profiling diagnostics; separate from actual-window startup timing',
                'before_profile_executable_hash_not_recorded':True,
                'optimized_profile_output_sha256':'731e3f714cf9125fdc63d2fe2e4054b271aba03a36f6d73bb87e8aec3e6a43b8',
                'before_bytes_required_by_old_prefix_copy':profile['direct3d9']['source_bytes_before_crop'],'after_bytes_staged':profile['direct3d9']['source_bytes_staged'],
                'before_api_ms':find_time(old_profile),'after_api_ms':find_time(profile),'before_report':reference(old_profile_path),'after_report':reference(profile_path)},
            'bridge_suite':reference(ROOT/'logs/bridge-suite.json'),'all_imports_registered':read('win64-bridge-tests.json')['registered_imports']==225,
            'full_api_conformance':False,'full_gameplay_verified':False}
    (ROOT/'logs/startup-optimization.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({key:result[key] for key in ['output_sha256','menu_elapsed_before_seconds','menu_elapsed_after_seconds','menu_elapsed_reduction_percent','remaining_menu_gap_vs_original_seconds']},indent=2))

if __name__=='__main__':main()
