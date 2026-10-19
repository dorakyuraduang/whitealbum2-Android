"""Decode original WMV/WMA files into owned memory, never a game/window/device."""
import ctypes
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[1]


def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    if os.name=='nt':ctypes.windll.kernel32.SetPriorityClass(ctypes.windll.kernel32.GetCurrentProcess(),0x4000)
    directory=ROOT/'build/native-canvas';directory.mkdir(parents=True,exist_ok=True)
    inputs=[p for folder in ('engine','platform') for p in (ROOT/folder).glob('*')
        if p.is_file() and p.suffix in ('.hpp','.cpp','.cmake','.txt','.inc')]
    inputs +=[ROOT/'tests/native_movie.cpp',ROOT/'tests/native_effects.cpp',ROOT/'tests/native_masks.cpp',ROOT/'tests/native_weather.cpp',ROOT/'tests/native_dialogue.cpp',ROOT/'tests/native_bitmaps.cpp',ROOT/'tests/canvas/CMakeLists.txt',Path(__file__),
        ROOT/'build/source-analysis/functions/004167e0.c',ROOT/'build/source-analysis/functions/0040b890.c',
        ROOT/'build/source-analysis/functions/004401b0.c',ROOT/'build/source-analysis/functions/004078b0.c',
        ROOT/'build/source-analysis/functions/004075a0.c',ROOT/'build/source-analysis/functions/0040fe50.c']
    bindings={p.relative_to(ROOT).as_posix():digest(p) for p in inputs}
    cmake=ROOT/'tools/python/cmake/data/bin/cmake.exe';flags=subprocess.BELOW_NORMAL_PRIORITY_CLASS|subprocess.CREATE_NO_WINDOW
    environment=os.environ.copy();environment.update(ZIG_GLOBAL_CACHE_DIR=str(ROOT/'build/zig-cache'),ZIG_LOCAL_CACHE_DIR=str(ROOT/'build/zig-local'),
        TEMP=str(ROOT/'build/temp'),TMP=str(ROOT/'build/temp'),CMAKE_BUILD_PARALLEL_LEVEL='1')
    with (ROOT/'logs/native-movie-build.log').open('wb') as console:
        for command in ([str(cmake),'-S',str(ROOT/'tests/canvas'),'-B',str(directory),'-G','Ninja',
            '-DCMAKE_TOOLCHAIN_FILE='+str(ROOT/'scripts/zig-toolchain.cmake'),'-DCMAKE_BUILD_TYPE=Release','-DWA2_TEST_MOVIE=ON'],
            [str(cmake),'--build',str(directory),'--target','wa2_movie_tests','wa2_effects_tests','wa2_masks_tests','wa2_weather_tests','wa2_dialogue_tests','wa2_bitmap_tests','--parallel','1']):
            subprocess.run(command,cwd=ROOT,env=environment,creationflags=flags,stdout=console,stderr=subprocess.STDOUT,check=True,timeout=900)
    program=directory/'wa2_movie_tests.exe';result=directory/'native-movie-tests.json'
    with (ROOT/'logs/native-movie-tests.log').open('wb') as console:
        effects=directory/'native-effects-tests.json'
        subprocess.run([str(directory/'wa2_effects_tests.exe'),str(ROOT/'res'),str(effects)],cwd=ROOT,creationflags=flags,
            stdout=console,stderr=subprocess.STDOUT,check=True,timeout=10)
        masks=directory/'native-masks-tests.json'
        subprocess.run([str(directory/'wa2_masks_tests.exe'),str(ROOT/'res'),str(masks)],cwd=ROOT,creationflags=flags,
            stdout=console,stderr=subprocess.STDOUT,check=True,timeout=15)
        weather=directory/'native-weather-tests.json'
        subprocess.run([str(directory/'wa2_weather_tests.exe'),str(ROOT/'res'),str(weather)],cwd=ROOT,creationflags=flags,
            stdout=console,stderr=subprocess.STDOUT,check=True,timeout=15)
        dialogue=directory/'native-dialogue-tests.json'
        subprocess.run([str(directory/'wa2_dialogue_tests.exe'),str(ROOT/'res'),str(dialogue)],cwd=ROOT,creationflags=flags,
            stdout=console,stderr=subprocess.STDOUT,check=True,timeout=20)
        bitmaps=directory/'native-bitmaps-tests.json'
        subprocess.run([str(directory/'wa2_bitmap_tests.exe'),str(ROOT/'res'),str(bitmaps)],cwd=ROOT,creationflags=flags,
            stdout=console,stderr=subprocess.STDOUT,check=True,timeout=20)
        subprocess.run([str(program),str(ROOT/'res'),str(result)],cwd=ROOT,creationflags=flags,
            stdout=console,stderr=subprocess.STDOUT,check=True,timeout=45)
    for name,value in bindings.items():
        if digest(ROOT/name)!=value:raise ValueError('Movie verification source changed: '+name)
    proof=json.loads(result.read_text());proof.update(source_sha256=bindings,test_program_sha256=digest(program),
        sound_effects_and_wave_verification=json.loads(effects.read_text()),
        transition_mask_verification=json.loads(masks.read_text()),
        weather_verification=json.loads(weather.read_text()),
        dialogue_verification=json.loads(dialogue.read_text()),
        bitmap_verification=json.loads(bitmaps.read_text()),
        generic_decoder_build=json.loads((ROOT/'logs/media-codec-windows-build.json').read_text()),
        android_ndk_link_verified=False,android_device_verified=False,published_runtime_changed=False)
    (ROOT/'logs/native-movie-verification.json').write_text(json.dumps(proof,indent=2)+'\n')
    print('PASS original WMV3/WMA2 native playback; actual scene next command',proof['next_unimplemented_command'],proof['next_unimplemented_command_name'],
        '(owned memory only; not a playable game APK).')


if __name__=='__main__':main()
