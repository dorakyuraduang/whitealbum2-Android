"""Verify actual story in the same scene host linked by Android; no game UI."""
import ctypes
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[1]


def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    ctypes.windll.kernel32.SetPriorityClass(ctypes.windll.kernel32.GetCurrentProcess(),0x4000)
    build=ROOT/'build/native-canvas';cmake=ROOT/'tools/python/cmake/data/bin/cmake.exe'
    inputs=[p for folder in ('engine','platform','game') for p in (ROOT/folder).glob('*') if p.suffix in ('.cpp','.hpp','.inc','.cmake','.txt')]
    inputs +=[Path(__file__),ROOT/'tests/native_game_scene.cpp',ROOT/'tests/native_characters.cpp',ROOT/'tests/native_voices.cpp',ROOT/'tests/native_effects.cpp',ROOT/'tests/native_input.cpp',ROOT/'tests/canvas/CMakeLists.txt']
    bindings={p.relative_to(ROOT).as_posix():digest(p) for p in inputs}
    environment=os.environ.copy();environment.update(ZIG_GLOBAL_CACHE_DIR=str(ROOT/'build/zig-cache'),ZIG_LOCAL_CACHE_DIR=str(ROOT/'build/zig-local'),TEMP=str(ROOT/'build/temp'),TMP=str(ROOT/'build/temp'),CMAKE_BUILD_PARALLEL_LEVEL='1')
    flags=subprocess.BELOW_NORMAL_PRIORITY_CLASS|subprocess.CREATE_NO_WINDOW
    with (ROOT/'logs/native-game-scene-build.log').open('wb') as console:
        for command in ([str(cmake),'-S',str(ROOT/'tests/canvas'),'-B',str(build),'-G','Ninja','-DCMAKE_TOOLCHAIN_FILE='+str(ROOT/'scripts/zig-toolchain.cmake'),'-DCMAKE_BUILD_TYPE=Release','-DWA2_TEST_MOVIE=ON'],
                        [str(cmake),'--build',str(build),'--target','wa2_game_scene_tests','wa2_character_tests','wa2_voice_tests','wa2_effects_tests','wa2_input_tests','--parallel','1']):
            subprocess.run(command,cwd=ROOT,env=environment,creationflags=flags,stdout=console,stderr=subprocess.STDOUT,check=True,timeout=900)
    character_report=build/'native-character-tests.json'
    with (ROOT/'logs/native-character-tests.log').open('wb') as console:
        subprocess.run([str(build/'wa2_character_tests.exe'),str(ROOT/'res'),str(character_report)],cwd=ROOT,creationflags=flags,stdout=console,stderr=subprocess.STDOUT,timeout=15,check=True)
    voice_report=build/'native-voice-tests.json'
    with (ROOT/'logs/native-voice-tests.log').open('wb') as console:
        subprocess.run([str(build/'wa2_voice_tests.exe'),str(voice_report)],cwd=ROOT,creationflags=flags,stdout=console,stderr=subprocess.STDOUT,timeout=10,check=True)
    extra={}
    for name,args in (('effects',[str(ROOT/'res')]),('input',[])):
        path=build/f'native-{name}-tests.json'
        with (ROOT/f'logs/native-scene-{name}-tests.log').open('wb') as console:
            subprocess.run([str(build/f'wa2_{name}_tests.exe'),*args,str(path)],cwd=ROOT,creationflags=flags,stdout=console,stderr=subprocess.STDOUT,timeout=20,check=True)
        extra[name]=json.loads(path.read_text())
    report=build/'native-game-scene-tests.json';program=build/'wa2_game_scene_tests.exe'
    with (ROOT/'logs/native-game-scene-tests.log').open('wb') as console:
        subprocess.run([str(program),str(ROOT/'res'),str(report)],cwd=ROOT,creationflags=flags,stdout=console,stderr=subprocess.STDOUT,timeout=45,check=True)
    for name,value in bindings.items():
        if digest(ROOT/name)!=value:raise ValueError('Native scene input changed: '+name)
    proof=json.loads(report.read_text());proof.update(source_sha256=bindings,test_program_sha256=digest(program),characters=json.loads(character_report.read_text()),voices=json.loads(voice_report.read_text()),extra_checks=extra,android_device_verified=False,apk_created=False)
    (ROOT/'logs/native-game-scene-verification.json').write_text(json.dumps(proof,indent=2)+'\n')
    print('PASS shared native game scene:',proof['original_messages'],'original dialogue messages;',proof['boundary'])


if __name__=='__main__':main()
