"""Bounded synthetic render-function comparisons; no game, UI or GPU device."""
import ctypes
import json
import os
from pathlib import Path
import statistics
import subprocess

ROOT=Path(__file__).resolve().parents[1]


def main():
    kernel=ctypes.WinDLL('kernel32',use_last_error=True);kernel.GetCurrentProcess.restype=ctypes.c_void_p
    kernel.SetPriorityClass.argtypes=[ctypes.c_void_p,ctypes.c_uint]
    if not kernel.SetPriorityClass(kernel.GetCurrentProcess(),0x4000):raise ctypes.WinError(ctypes.get_last_error())
    flags=subprocess.CREATE_NO_WINDOW|subprocess.BELOW_NORMAL_PRIORITY_CLASS
    build=ROOT/'build/shared-aot-x64';current=build/'wa2_render_cpu_tests.exe'
    previous=build/'wa2_render_cpu_before_0.2.10.exe'
    result={'schema':'wa2.bounded-render-register-comparison.v1','comparisons':[],
            'maximum_build_jobs':1,'whole_game_fps_measured':False,'game_launched':False,
            'graphics_device_created':False,'screen_access':False,'source_executable_opened':False}
    with (ROOT/'logs/render-register-comparison.log').open('wb') as console:
        configurations=[('unchanged_legacy_fixture',previous,['--pc24','--dense'],['--pc24','--dense']),
                        ('unchanged_small_texture_fixture',previous,['--pc24'],['--pc24']),
                        ('small_gradient_quad_register_file',current,['--pc24','--varied','--serialized'],['--pc24','--varied']),
                        ('gradient_quad_register_file',current,['--pc24','--dense','--varied','--serialized'],['--pc24','--dense','--varied']),
                        ('masked_quad_register_file',current,['--pc24','--masked','--varied','--serialized'],['--pc24','--masked','--varied'])]
        for name,before_exe,before_options,after_options in configurations:
            measurements=[[],[]];reports=[[],[]]
            for round,after in enumerate([False,True,True,False]):
                path=ROOT/'logs'/f'render-register-{name}-{round}.json'
                command=[str(current if after else before_exe),str(path),*(after_options if after else before_options)]
                subprocess.run(command,cwd=ROOT,creationflags=flags,stdout=console,stderr=subprocess.STDOUT,check=True,timeout=30)
                report=json.loads(path.read_text());assert report['passed'] and not report['game_launched'] and not report['graphics_device_created']
                assert report['source_fp_control']==127 and report['bounded_replay']['calls_per_mode']==20
                measurements[after].append(report['bounded_replay']['mode_ns'][2]);reports[after].append(report)
            before,after=[statistics.mean(values) for values in measurements]
            counts=[r['cases'][2]['device_calls'] for group in reports for r in group]
            assert all(count==counts[0] for count in counts)
            result['comparisons'].append({'input':name,'source_fp_control':127,'calls_per_sample':20,
                'samples_ns_before':measurements[0],'samples_ns_after':measurements[1],
                'mean_ns_before':before,'mean_ns_after':after,'time_reduction_percent':100*(before-after)/before,
                'speed_ratio':before/after,'draws_per_call':counts[0]['DrawPrimitiveUP'],
                'same_device_calls':True,'exact_cpu_stack_and_vertices':all(c['exact_cpu_stack_and_vertices'] for group in reports for r in group for c in r['cases'])})
    result['passed']=True
    (ROOT/'logs/render-register-comparison-0.2.10.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))


if __name__=='__main__':main()
