"""Compare bounded particle calls to 0.2.19 without launching a game or device."""
import ctypes
import hashlib
import json
from pathlib import Path
import statistics
import subprocess

ROOT=Path(__file__).resolve().parents[1]


def load(path):return json.loads(path.read_text(encoding='utf-8'))


def main():
    kernel=ctypes.WinDLL('kernel32');kernel.GetCurrentProcess.restype=ctypes.c_void_p
    kernel.SetPriorityClass.argtypes=[ctypes.c_void_p,ctypes.c_uint]
    if not kernel.SetPriorityClass(kernel.GetCurrentProcess(),0x4000):raise ctypes.WinError()
    kernel.SetErrorMode(0x8003)
    from prepare_particle_baseline import main as prepare
    prepare()
    flags=subprocess.CREATE_NO_WINDOW|subprocess.BELOW_NORMAL_PRIORITY_CLASS
    build=ROOT/'build/shared-aot-x64';folder=ROOT/'build/performance-0.2.20'
    binaries=[folder/'weather-before.exe',build/'wa2_weather_cpu_tests.exe'];rounds=[]
    required_checks={'native_weather_prefix_snapshots':6,'native_weather_control_snapshots':10,
        'native_weather_host_environment_snapshots':4,'native_weather_decline_snapshots':6}
    with (ROOT/'logs/particle-comparison.log').open('wb') as log:
        for pair in range(3):
            values=[None,None]
            for order in range(2):
                side=(pair+order)&1;path=folder/f'weather-{pair}-{side}.json'
                subprocess.run([str(binaries[side]),str(path)],cwd=ROOT,creationflags=flags,
                    stdout=log,stderr=subprocess.STDOUT,check=True,timeout=90)
                values[side]=load(path)
                if not values[side].get('passed'):raise ValueError('Particle state checks did not pass')
                if side and any(values[side].get(key)!=value for key,value in required_checks.items()):
                    raise ValueError('Missing weather prefix, precision, environment or rollback checks')
            if len(values[0]['cases'])!=len(values[1]['cases']):raise ValueError('Particle fixture count changed')
            for before,after in zip(values[0]['cases'],values[1]['cases']):
                keys=('particles','updates','vector_clear_enabled','ram_sha256','exact_cpu_fp_and_ram')
                if any(before[key]!=after[key] for key in keys):raise ValueError('Particle CPU/FP/RAM fixture changed')
            rounds.append(values)
        work_path=folder/'weather-work.json'
        subprocess.run([str(binaries[1]),str(work_path),'--work'],cwd=ROOT,creationflags=flags,
            stdout=log,stderr=subprocess.STDOUT,check=True,timeout=90)
    work=load(work_path)
    if not work.get('passed') or len(work.get('cases',[]))!=8:raise ValueError('Missing bounded weather path samples')
    for case in work['cases']:
        batch=case['work']['weather_batch'];updates=case['updates']
        if case['particles']==16:
            if batch['attempts']:raise ValueError('Small populations must skip register-cache serialization for weather batches')
            continue
        if (batch['attempts']!=updates or batch['completed']!=updates or batch['yields'] or
                batch['color_completed']!=1600*updates or batch['native_operations']<10000*updates):
            raise ValueError('Ordinary particles missed the complete native batch path')
    result=dict(schema='wa2.particle-component-comparison.v1',release=load(ROOT/'config/release.json')['name'],
        baseline_release='0.2.19-shared-aot',cases=[],complete_gameplay_verified=False,
        game_launched=False,screen_access=False,device_opened=False,whole_game_fps_measured=False,
        overall_tenfold_speedup_established=False)
    result['snapshot_checks_per_run']=required_checks
    result['bounded_native_path_samples']=work
    for index,record in enumerate(rounds[0][0]['cases']):
        pairs=[[statistics.median(p[0]['cases'][index]['elapsed_ns']),statistics.median(p[1]['cases'][index]['elapsed_ns'])]for p in rounds]
        if any(min(pair)<=0 for pair in pairs):raise ValueError('Particle timing is below timer resolution')
        result['cases'].append(dict(particles=record['particles'],updates=record['updates'],
            vector_clear_enabled=bool(record['vector_clear_enabled']),paired_ns=pairs,
            median_paired_speedup=statistics.median(a/b for a,b in pairs),
            median_ns_per_update=[statistics.median(pair[side]/record['updates'] for pair in pairs) for side in range(2)],
            same_ram_sha256=True,exact_cpu_fp_and_ram=True))
    result['binaries_sha256']={str(path.relative_to(ROOT)):hashlib.sha256(path.read_bytes()).hexdigest() for path in binaries}
    result['limits']=['One or two bounded complete weather updates include source sprite construction and setters in RAM.',
        'The second update preserves existing sprites and weather state. This is not a game/frame loop.',
        'No GPU, presentation, Android CPU or game FPS was measured. Component gains do not establish whole-game gains.']
    result['passed']=True
    version=result['release'].removesuffix('-shared-aot')
    (ROOT/f'logs/performance-particles-{version}.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps([dict(particles=x['particles'],updates=x['updates'],vector=x['vector_clear_enabled'],speedup=x['median_paired_speedup']) for x in result['cases']]))


if __name__=='__main__':main()
