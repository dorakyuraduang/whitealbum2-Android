"""Bind resource churn, ordinary-run counters and package size to one EXE."""
from pathlib import Path
import hashlib
import json

ROOT=Path(__file__).resolve().parents[1]
PREVIOUS='c68028ff6e94563bd9e17ebc81eee7f78cb6d19d86e1c29369e8f8f9d45fd32e'

def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def bound(path):return dict(file=str(Path(path).resolve()),sha256=digest(path))
def read(path):return json.loads(Path(path).read_text())

def verification():
    output=ROOT/'out/WA2_SDL2_x64.exe';value=digest(output)
    resource=ROOT/'res/WA2_SDL2_x64.exe'
    assert digest(resource)==value
    suite_path=ROOT/'logs/execution-suite.json';suite=read(suite_path)
    assert suite['output_sha256']==value and len(suite['fixtures'])==21 and suite['core_tests']==10
    assert not suite['screen_access'] and all(x['return_code']==0 for x in suite['fixtures'])
    for fixture in suite['fixtures']:
        assert digest(fixture['log'])==fixture['log_sha256']
        if 'report' in fixture:assert digest(fixture['report'])==fixture['report_sha256']
    assert digest(suite['core_log'])==suite['core_log_sha256']
    baseline_path=ROOT/'logs/heap-churn-baseline.json';baseline=read(baseline_path)
    assert baseline['expected_failure'] and baseline['return_code']==1
    for item in baseline['files'].values():assert digest(item['file'])==item['sha256']
    heap_path=ROOT/'logs/heap-churn-tests.json';heap=read(heap_path)
    assert heap['small_allocations']==32768 and heap['regions_restored'] and heap['reserved_bytes_at_peak']<3*1024*1024
    graphics=next(x for x in suite['fixtures'] if x['fixture']=='wa2_graphics_tests')
    graphics_text=Path(graphics['log']).read_text()
    assert 'PASS 6000 offscreen texture/surface upload/release cycles' in graphics_text
    assert 'PASS 256 sound buffer lifetimes' in graphics_text
    ordinary_path=ROOT/'logs/resource-ordinary-300s.json';ordinary=read(ordinary_path)
    assert ordinary['output_sha256']==value and ordinary['launch_mode']=='ordinary'
    assert ordinary['requested_seconds']==300 and ordinary['elapsed_seconds']>=300 and ordinary['timed_measurement_stop']
    assert not ordinary['error'] and not ordinary['screen_access'] and not ordinary['frames_captured'] and not ordinary['profile_enabled']
    assert digest(ordinary['program_archive']['file'])==value
    assert digest(ordinary['report_file'])==ordinary['report_sha256']
    runtime=read(ordinary['report_file']);state=runtime.get('process',runtime)
    assert not runtime.get('error') and not state['exited'] and ordinary['program_frames']>0
    assert ordinary['audio'] and all(not x['missed_decode_deadlines'] and not x['writes_into_playing_half'] for x in ordinary['audio'])
    assert state['guest_allocations']['version']=='heap-pools-2026-10-16'
    origin=ordinary['cpu_samples'][0][0];hz=state['direct3d9']['presentation_clock_hz']
    steady=[x for x in ordinary['allocation_samples'] if x['qpc']-origin>=60*hz]
    assert len(steady)>=120
    ranges={}
    for group,fields in [('guest',('regions','reserved_bytes','heap_blocks','heap_requested_bytes')),
                         ('graphics',('live_objects','resource_entries','texture_entries','image_entries')),
                         ('host_memory',('working_set_bytes','private_bytes'))]:
        ranges[group]={k:dict(min=min(x[group][k] for x in steady),max=max(x[group][k] for x in steady),
                             first=steady[0][group][k],last=steady[-1][group][k]) for k in fields}
    before=ROOT/'build/runtime-backups'/PREVIOUS/'WA2_SDL2_x64.exe';assert digest(before)==PREVIOUS
    package=dict(before=bound(before),before_bytes=before.stat().st_size,after_bytes=output.stat().st_size,
                 reduction_percent=100*(1-output.stat().st_size/before.stat().st_size),
                 output_contains_pdb=False,paged_native_backend_shipped=False)
    assert not list((ROOT/'out').glob('*.pdb')) and not (ROOT/'out/WA2_x64.exe').exists()
    assert 'WA2_PAGED_NATIVE_BACKEND:BOOL=OFF' in (ROOT/'build/wa2-win64/CMakeCache.txt').read_text()
    sources=['port/weak_registry.hpp','port/direct3d_sdl.cpp','port/audio.cpp','port/native_backend.cpp','port/CMakeLists.txt',
             'port/wa2_win32.cpp','tests/heap_churn.cpp','tests/graphics.cpp',
             'tools/WindowsRecompTool-main/include/winrecomp/win32_state.hpp',
             'tools/WindowsRecompTool-main/src/runtime/win32.cpp','tools/WindowsRecompTool-main/src/runtime/process.cpp']
    excluded=[]
    for name in ('resource-ordinary-300s','resource-matched-wall'):
        path=ROOT/'logs'/(name+'.json');record=read(path)
        if not record.get('movie_completed_naturally'):
            excluded.append(dict(evidence=bound(path),reason='Opening did not complete naturally; excluded from original/native FPS acceptance.'))
    return dict(schema='wa2.resource-repair.v1',output_sha256=value,execution_suite=bound(suite_path),
        source_files=[bound(ROOT/name) for name in sources],old_heap_regression=bound(baseline_path),
        heap_stress=dict(evidence=bound(heap_path),result=heap),graphics_churn=dict(textures=6000,sound_buffers=256,evidence=bound(graphics['log'])),
        ordinary=dict(evidence=bound(ordinary_path),runtime=bound(ordinary['report_file']),seconds=ordinary['elapsed_seconds'],
                      frames=ordinary['program_frames'],steady_samples=len(steady),steady_ranges=ranges,audio=ordinary['audio'],
                      ended_by_test_timer=True,normal_exit_verified=False),package=package,excluded_performance=excluded,
        screen_access=False,automated_input=False,actual_fast_forward_verified=False,gameplay_verified=False)

if __name__=='__main__':
    result=verification();(ROOT/'logs/resource-lifetime-verification.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(dict(output_sha256=result['output_sha256'],heap_stress=result['heap_stress']['result'],
                         ordinary=result['ordinary'],package=result['package']),indent=2))
