"""Compare bounded complete weather calls and audio cores, never scene FPS."""
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
    flags=subprocess.CREATE_NO_WINDOW|subprocess.BELOW_NORMAL_PRIORITY_CLASS
    build=ROOT/'build/shared-aot-x64';folder=ROOT/'build/performance-0.2.19'
    from prepare_weather_baseline import main as prepare_baseline
    prepare_baseline()
    with (ROOT/'logs/particle-audio-comparison.log').open('wb') as log:
        def run(binary,path,extra=()):
            subprocess.run([str(binary),*map(str,extra),str(path)],cwd=ROOT,creationflags=flags,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=90)
            data=load(path)
            if not data.get('passed'):raise ValueError('Memory fixture did not pass: '+str(path))
            return data
        # Alternate process order across rounds to avoid always crediting the
        # second executable with warmed CPU/file caches.
        weather=[];audio=[]
        for pair in range(3):
            values=[None,None];sound=[None,None]
            for order in range(2):
                side=(pair+order)&1
                values[side]=run(build/'wa2_weather_cpu_tests.exe' if side else folder/'weather-before.exe',folder/f'weather-{pair}-{side}.json')
                sound[side]=run(build/('wa2_audio_latency_tests.exe' if side else 'wa2_audio_latency_before.exe'),folder/f'audio-{pair}-{side}.json',[ROOT/'res/bgm.pak'])
            if len(values[0]['cases'])!=len(values[1]['cases']):raise ValueError('Weather fixture count changed')
            for before,after in zip(values[0]['cases'],values[1]['cases']):
                if (before['particles'],before['updates'],before['vector_clear_enabled'],before['ram_sha256'])!=(after['particles'],after['updates'],after['vector_clear_enabled'],after['ram_sha256']):
                    raise ValueError('Weather CPU/RAM fixture differs between releases')
            for before,after in zip(values[0]['buffer_initialization'],values[1]['buffer_initialization']):
                keys=('bytes','vector_clear_enabled','destination_offset','ram_sha256')
                if any(before[key]!=after[key] for key in keys):raise ValueError('Buffer initialization fixture differs between releases')
            if sound[0]['pcm_sha256']!=sound[1]['pcm_sha256']:raise ValueError('PCM sample bits changed')
            for before,after in zip(sound[0]['tracks'],sound[1]['tracks']):
                if (before['member'],before['pcm_bytes'],before['pcm_sha256'])!=(after['member'],after['pcm_bytes'],after['pcm_sha256']):
                    raise ValueError('Vorbis PCM prefix differs between optimization levels')
            weather.append(values);audio.append(sound)
    result=dict(schema='wa2.particle-audio-component-comparison.v1',release=load(ROOT/'config/release.json')['name'],
        baseline_release='0.2.18-shared-aot',weather=[],audio=dict(),interface_handles=load(build/'interface-handles.json'),
        native_strings=load(build/'native-string.json'),
        complete_gameplay_verified=False,game_launched=False,screen_access=False,device_fps_measured=False,overall_tenfold_speedup_established=False)
    for case in range(len(weather[0][0]['cases'])):
        pairs=[[statistics.median(p[0]['cases'][case]['elapsed_ns']),statistics.median(p[1]['cases'][case]['elapsed_ns'])]for p in weather]
        result['weather'].append(dict(particles=weather[0][0]['cases'][case]['particles'],updates=weather[0][0]['cases'][case]['updates'],paired_ns=pairs,
            vector_clear_enabled=bool(weather[0][0]['cases'][case]['vector_clear_enabled']),
            median_paired_speedup=statistics.median(a/b for a,b in pairs),same_ram_sha256=True,exact_cpu_fp_and_ram=True))
    result['buffer_initialization']=[]
    for case,record in enumerate(weather[0][0]['buffer_initialization']):
        pairs=[[statistics.median(p[0]['buffer_initialization'][case]['elapsed_ns']),statistics.median(p[1]['buffer_initialization'][case]['elapsed_ns'])]for p in weather]
        result['buffer_initialization'].append(dict(bytes=record['bytes'],vector_clear_enabled=bool(record['vector_clear_enabled']),
            destination_offset=record['destination_offset'],paired_ns=pairs,median_paired_speedup=statistics.median(a/b for a,b in pairs),
            same_ram_sha256=True,exact_cpu_fp_and_ram=True))
    pairs=[[statistics.median(p[0]['mix_ns']),statistics.median(p[1]['mix_ns'])]for p in audio]
    result['audio']['mixing']=dict(paired_ns=pairs,median_paired_speedup=statistics.median(a/b for a,b in pairs),same_pcm_sha256=True)
    result['audio']['cold_vorbis']=[]
    for index in range(len(audio[0][0]['tracks'])):
        pairs=[[statistics.median(p[0]['tracks'][index]['cold_decode_ns']),statistics.median(p[1]['tracks'][index]['cold_decode_ns'])]for p in audio]
        result['audio']['cold_vorbis'].append(dict(member=audio[0][0]['tracks'][index]['member'],paired_ns=pairs,
            median_paired_speedup=statistics.median(a/b for a,b in pairs),same_pcm_sha256=True))
    if not result['interface_handles'].get('passed') or not result['native_strings'].get('passed'):
        raise ValueError('Missing interface lifetime or native string-operation verification')
    result['interface_handles']['median_paired_speedup']=statistics.median(a/b for a,b in result['interface_handles']['paired_ns'])
    for component in result['native_strings']['components']:
        if any(b<=0 for _,b in component['paired_ns']):raise ValueError('Bulk operation timing window is below clock resolution; increase bounded repetitions')
        component['median_paired_speedup']=statistics.median(a/b for a,b in component['paired_ns'])
    result['binaries_sha256']={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in
        (folder/'weather-before.exe',build/'wa2_weather_cpu_tests.exe',build/'wa2_audio_latency_before.exe',build/'wa2_audio_latency_tests.exe')}
    result['limits']=['Complete weather CPU calls include update and sprite setters, but no rendering or frame loop.',
        'Audio measurements cover bounded decoding/mixing in RAM; original PAK reads are outside timed sections.',
        'No graphics/audio device or Android CPU was run. Component gains cannot be multiplied into whole-game FPS.']
    result['passed']=True
    version=result['release'].removesuffix('-shared-aot')
    (ROOT/f'logs/performance-particle-audio-{version}.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(dict(weather=[(x['particles'],x['median_paired_speedup'])for x in result['weather']],
        mixing=result['audio']['mixing']['median_paired_speedup'],
        bulk_strings=[(x['bytes'],x['median_paired_speedup'])for x in result['native_strings']['components']])))


if __name__=='__main__':main()
