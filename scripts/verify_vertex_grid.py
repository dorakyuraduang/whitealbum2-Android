"""Bounded RAM-only geometry checks. No game, screen, GPU or frame loop."""
import ctypes
import json
import os
from pathlib import Path
import statistics
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]

def main():
    kernel=ctypes.WinDLL('kernel32');kernel.GetCurrentProcess.restype=ctypes.c_void_p
    kernel.SetPriorityClass.argtypes=[ctypes.c_void_p,ctypes.c_uint]
    if not kernel.SetPriorityClass(kernel.GetCurrentProcess(),0x4000):raise ctypes.WinError()
    flags=subprocess.CREATE_NO_WINDOW|subprocess.BELOW_NORMAL_PRIORITY_CLASS
    environment=os.environ.copy();environment.update(CMAKE_BUILD_PARALLEL_LEVEL='1',TEMP=str(ROOT/'build/temp'),TMP=str(ROOT/'build/temp'),ZIG_GLOBAL_CACHE_DIR=str(ROOT/'build/zig-cache'),ZIG_LOCAL_CACHE_DIR=str(ROOT/'build/zig-local'))
    build=ROOT/'build/shared-aot-x64';cmake=ROOT/'tools/python/cmake/data/bin/cmake.exe'
    with (ROOT/'logs/native-grid-build.log').open('wb') as console:
        def run(command,timeout=30):
            command=[str(x) for x in command]
            with subprocess.Popen(command,cwd=ROOT,env=environment,creationflags=flags,stdout=console,stderr=subprocess.STDOUT) as child:
                try:status=child.wait(timeout)
                except subprocess.TimeoutExpired:
                    subprocess.run(['taskkill','/PID',str(child.pid),'/T','/F'],creationflags=flags,stdout=console,stderr=subprocess.STDOUT,timeout=20)
                    child.wait(20);raise
                if status:raise subprocess.CalledProcessError(status,command)
        run([cmake,'--build',build,'--target','wa2_shared_aot_tests','wa2_render_cpu_tests','--parallel','1'],1200)
        run([build/'wa2_shared_aot_tests.exe',ROOT/'logs/native-grid-tests.json','--grid-only'],90)
        if '--compare' not in sys.argv:return
        result={'schema':'wa2.bounded-native-geometry-comparison.v1','comparisons':[],'maximum_build_jobs':1,
                'whole_game_fps_measured':False,'game_launched':False,'graphics_device_created':False,'screen_access':False}
        for name,common in [('small_gradient',['--pc24','--varied']),('dense_gradient',['--pc24','--varied','--dense']),
                            ('masked_unchanged_control',['--pc24','--varied','--masked'])]:
            path=ROOT/'logs'/f'native-grid-paired-{name}.json'
            run([build/'wa2_render_cpu_tests.exe',path,*common,'--compare-grid'])
            report=json.loads(path.read_text());assert report['passed']
            pairs=report['paired_grid_ns'];assert len(pairs)==8
            values=list(zip(*pairs));old,new=[statistics.median(v) for v in values]
            calls=report['cases'][2]['device_calls']
            result['comparisons'].append(dict(input=name,before_option='same binary, native vertex grid disabled',calls_per_sample=16,paired_ns=pairs,
                median_ns_before=old,median_ns_after=new,median_paired_time_reduction_percent=statistics.median(100*(a-b)/a for a,b in pairs),
                draws_per_call=calls['DrawPrimitiveUP'],after_grid_counts=report['vertex_grid'],same_device_calls=True,exact_cpu_stack_and_vertices=True))
        result['passed']=True
        (ROOT/'logs/native-grid-comparison-0.2.11.json').write_text(json.dumps(result,indent=2)+'\n')
        print(json.dumps(result,indent=2))

if __name__=='__main__':main()
