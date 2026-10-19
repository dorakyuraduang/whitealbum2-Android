"""Compare two bounded synthetic CPU function checks; no game or GPU.

The saved 0.2.17 diagnostic executable is paired with the current diagnostic
executable. Both run the same fixed inputs, with an in-memory graphics API.
This is not a frame-rate, device, or whole-game performance measurement.
"""
import ctypes
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess

ROOT=Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    kernel.GetCurrentProcess.restype=ctypes.c_void_p
    kernel.SetPriorityClass.argtypes=[ctypes.c_void_p,ctypes.c_uint]
    if not kernel.SetPriorityClass(kernel.GetCurrentProcess(),0x4000):raise ctypes.WinError()
    kernel.SetErrorMode(0x8003)
    flags=subprocess.CREATE_NO_WINDOW|subprocess.BELOW_NORMAL_PRIORITY_CLASS
    env=os.environ.copy()
    folder=ROOT/'build/performance-0.2.18'
    before=folder/'render-cpu-before.exe'
    after=ROOT/'build/shared-aot-x64/wa2_render_cpu_tests.exe'
    receipt=json.loads((ROOT/'logs/performance-0.2.18-baseline.json').read_text(encoding='utf-8'))
    if Path(receipt['binary']).resolve()!=before or digest(before)!=receipt['sha256']:
        raise ValueError('The saved diagnostic baseline does not match its receipt')
    result=dict(schema='wa2.bounded-particle-cpu-comparison.v1',before_release=receipt['baseline_release'],
                after_release=json.loads((ROOT/'config/release.json').read_text())['name'],
                before_binary_sha256=digest(before),after_binary_sha256=digest(after),
                fixture='the same checked sprite function, synthetic RAM, in-memory device',
                maximum_jobs=1,below_normal_priority=True,graphics_device_created=False,
                game_launched=False,screen_access=False,whole_game_fps_measured=False,comparisons=[])
    with (ROOT/'logs/particle-cpu-comparison.log').open('wb') as console:
        for name,options in [('ordinary',['--pc24','--varied']),('tiled',['--pc24','--varied','--dense']),
                             ('masked',['--pc24','--varied','--masked'])]:
            pairs=[]
            for pair in range(4):
                values=[None,None]
                for order in range(2):
                    which=(pair+order)&1
                    report=folder/f'paired-{name}-{pair}-{which}.json'
                    subprocess.run([str(after if which else before),str(report),*options,'--compare-all'],
                                   cwd=ROOT,env=env,creationflags=flags,stdout=console,
                                   stderr=subprocess.STDOUT,check=True,timeout=30)
                    data=json.loads(report.read_text())
                    if not data['passed'] or any(not case['exact_cpu_stack_and_vertices'] for case in data['cases']):
                        raise ValueError('Synthetic source state comparison failed')
                    values[which]=dict(thread_cycles=statistics.median(row[1] for row in data['paired_pipeline_thread_cycles']),
                                       ns=statistics.median(row[1] for row in data['paired_pipeline_ns']),
                                       bounded_mode_ns=data['bounded_replay']['mode_ns'],
                                       calls=[case['device_calls'] for case in data['cases']],report=str(report))
                if values[0]['calls']!=values[1]['calls']:raise ValueError('Diagnostic inputs produced different device calls')
                pairs.append(values)
            ratios=[pair[0]['thread_cycles']/pair[1]['thread_cycles'] for pair in pairs]
            result['comparisons'].append(dict(input=name,calls_per_sample=16,process_pairs=pairs,
                median_paired_thread_cycle_speedup=statistics.median(ratios),
                bounded_mode_time_speedup={mode:statistics.median(pair[0]['bounded_mode_ns'][index]/pair[1]['bounded_mode_ns'][index] for pair in pairs)
                    for index,mode in enumerate(('solid_quad','clear','textured_quad'))},
                exact_cpu_stack_and_vertices=True,same_device_calls=True))
    result['limits']=[
        'Includes the fixed fixture capture/allocation overhead in both binaries.',
        'Native CRT replacements are disabled in both CPU fixtures for exact state comparison.',
        'Does not measure the new compact GPU stream, the real scene update, resource I/O or device FPS.',
        'No overall tenfold speedup is established by these component results.']
    result['passed']=True
    (ROOT/'logs/particle-cpu-comparison-0.2.18.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({row['input']:row['median_paired_thread_cycle_speedup'] for row in result['comparisons']}))


if __name__=='__main__':main()
