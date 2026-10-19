"""Bounded RAM-only ABI and source-function checks. No game or device runs."""
import ctypes
import json
import os
from pathlib import Path
import statistics
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    kernel.GetCurrentProcess.restype=ctypes.c_void_p
    kernel.SetPriorityClass.argtypes=[ctypes.c_void_p,ctypes.c_uint]
    if not kernel.SetPriorityClass(kernel.GetCurrentProcess(),0x4000):raise ctypes.WinError(ctypes.get_last_error())
    environment=os.environ.copy()
    environment.update(CMAKE_BUILD_PARALLEL_LEVEL='1',TEMP=str(ROOT/'build/temp'),TMP=str(ROOT/'build/temp'),
                       ZIG_GLOBAL_CACHE_DIR=str(ROOT/'build/zig-cache'),ZIG_LOCAL_CACHE_DIR=str(ROOT/'build/zig-local'))
    flags=subprocess.CREATE_NO_WINDOW|subprocess.BELOW_NORMAL_PRIORITY_CLASS
    cmake=ROOT/'tools/python/cmake/data/bin/cmake.exe';build=ROOT/'build/shared-aot-x64'
    with (ROOT/'logs/fast-dispatch-build.log').open('wb') as console:
        def run(command,timeout=30):
            command=[str(part) for part in command]
            with subprocess.Popen(command,cwd=ROOT,env=environment,creationflags=flags,stdout=console,stderr=subprocess.STDOUT) as child:
                try:status=child.wait(timeout)
                except subprocess.TimeoutExpired:
                    subprocess.run(['taskkill','/PID',str(child.pid),'/T','/F'],creationflags=flags,
                                   stdout=console,stderr=subprocess.STDOUT,timeout=20)
                    child.wait(20);raise
                if status:raise subprocess.CalledProcessError(status,command)
        if '--no-build' not in sys.argv:
            run([sys.executable,ROOT/'scripts/generate_shared_aot.py'],120)
            run([cmake,'-S',ROOT/'recompiled','-B',build,'-G','Ninja',
                 '-DCMAKE_TOOLCHAIN_FILE='+str(ROOT/'scripts/zig-toolchain.cmake'),
                 '-DCMAKE_BUILD_TYPE=MinSizeRel','-DWA2_SHARED_AOT_CHECKS=ON','-DWA2_AOT_DIAGNOSTICS=ON',
                 '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON'])
            run([cmake,'--build',build,'--target','wa2_host_leaf_tests','wa2_portable_system_tests',
                 'wa2_render_cpu_tests','wa2_render_submission_tests','wa2_window_model_tests','wa2_integer_register_tests','--parallel','1'],2400)
            for name,target in [('abi','wa2_host_leaf_tests'),('strings','wa2_portable_system_tests'),
                                ('submission','wa2_render_submission_tests'),('window','wa2_window_model_tests'),('integers','wa2_integer_register_tests')]:
                run([build/(target+'.exe'),ROOT/'logs'/('fast-dispatch-'+name+'.json')])
        result=dict(schema='wa2.bounded-native-dispatch-comparison.v1',comparisons=[],maximum_build_jobs=1,
                    game_launched=False,screen_access=False,graphics_device_created=False,whole_game_fps_measured=False)
        for name,options in [('small_gradient',['--pc24','--varied']),
                             ('dense_gradient',['--pc24','--dense','--varied']),
                             ('masked_gradient',['--pc24','--masked','--varied'])]:
            path=ROOT/'logs'/('fast-dispatch-'+name+'.json')
            run([build/'wa2_render_cpu_tests.exe',path,*options,'--compare-services','--compare-all'])
            report=json.loads(path.read_text());assert report['passed']
            pairs=report['paired_pipeline_ns'];assert len(pairs)==8
            before,after=[statistics.median(values) for values in zip(*pairs)]
            case=report['cases'][2]
            cycle_pairs=report['paired_pipeline_thread_cycles'];assert len(cycle_pairs)==8 and all(a>0 and b>0 for a,b in cycle_pairs)
            result['comparisons'].append(dict(input=name,calls_per_sample=16,paired_ns=pairs,
                comparison='native integer locals, normalized float registers and direct device services on/off in the same binary',
                service_only_paired_ns=report['paired_services_ns'],
                paired_thread_cycles=cycle_pairs,median_paired_thread_cycle_speedup=statistics.median(a/b for a,b in cycle_pairs),
                median_paired_thread_cycle_reduction_percent=statistics.median(100*(a-b)/a for a,b in cycle_pairs),
                median_ns_before=before,median_ns_after=after,
                median_paired_speedup=statistics.median(a/b for a,b in pairs),
                median_paired_time_reduction_percent=statistics.median(100*(a-b)/a for a,b in pairs),
                outer_function_entries_before=case['outer_function_entries_before'],outer_function_entries_after=case['outer_function_entries_after'],
                outer_api_boundaries_before=case['outer_api_boundaries_before'],outer_api_boundaries_after=case['outer_api_boundaries_after'],
                direct_device_calls=case['direct_device_calls'],device_calls=case['device_calls'],exact_cpu_stack_and_vertices=True))
        result['passed']=True
        (ROOT/'logs/fast-dispatch-comparison-0.2.12.json').write_text(json.dumps(result,indent=2)+'\n')
        print(json.dumps(result,indent=2))


if __name__=='__main__':main()
