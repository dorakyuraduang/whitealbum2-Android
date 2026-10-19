"""Check native archive-to-audio flow; no audio device, game, original EXE or screen."""
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[1]


def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    directory=ROOT/'build/native-audio-pipeline';directory.mkdir(parents=True,exist_ok=True)
    sources=[ROOT/p for p in ('engine/pcm_audio.cpp','engine/pcm_stream.cpp','engine/vorbis_stream.cpp','engine/ogg_reader.cpp','engine/wave_reader.cpp',
        'engine/stream_voice.cpp','engine/resource_archive.cpp','engine/scene_music.cpp','engine/scene_controls.cpp',
        'engine/script_runtime.cpp','engine/script_session.cpp','platform/file_io.cpp','tests/native_audio_pipeline.cpp')]
    headers={p.relative_to(ROOT).as_posix():digest(p) for folder in ('engine','platform') for p in (ROOT/folder).glob('*.hpp')}
    bindings={p.relative_to(ROOT).as_posix():digest(p) for p in [*sources,ROOT/'engine/CMakeLists.txt',Path(__file__),
        ROOT/'build/source-analysis/functions/0040fcb0.c',ROOT/'build/source-analysis/functions/004068a0.c',ROOT/'build/source-analysis/functions/0040faa0.c']}
    bindings.update(headers)
    environment=os.environ.copy();environment.update(ZIG_GLOBAL_CACHE_DIR=str(ROOT/'build/zig-cache'),ZIG_LOCAL_CACHE_DIR=str(ROOT/'build/zig-local'),TEMP=str(ROOT/'build/temp'),TMP=str(ROOT/'build/temp'))
    flags=(subprocess.BELOW_NORMAL_PRIORITY_CLASS|subprocess.CREATE_NO_WINDOW) if os.name=='nt' else 0
    common=[str(ROOT/'tools/python/ziglang/zig.exe'),'c++','-std=c++20','-O1','-g0','-ffp-contract=off',
        '-I'+str(ROOT/'engine'),'-I'+str(ROOT/'platform')]
    def run(command,console):
        subprocess.run(command,cwd=ROOT,env=environment,creationflags=flags,stdout=console,stderr=subprocess.STDOUT,timeout=120,check=True)
    def cached(command,destination,inputs,console):
        stamp=destination.with_suffix(destination.suffix+'.json');identity=dict(command=command,inputs=inputs)
        if stamp.is_file() and destination.is_file():
            previous=json.loads(stamp.read_text())
            if previous.get('identity')==identity and previous.get('sha256')==digest(destination):return
        run(command,console);stamp.write_text(json.dumps(dict(identity=identity,sha256=digest(destination)),indent=2)+'\n')
    program=directory/'native-audio-tests.exe';result=directory/'native-audio-tests.json';objects=[];arms=[]
    with (ROOT/'logs/native-audio-pipeline-build.log').open('wb') as console:
        for i,source in enumerate(sources):
            obj=directory/(str(i)+'-'+source.stem+'.obj');objects.append(obj)
            extra={'third_party/stb/stb_vorbis.c':digest(ROOT/'third_party/stb/stb_vorbis.c')} if source.stem=='vorbis_stream' else {}
            cached([*common,'-target','x86_64-windows-gnu','-c',str(source),'-o',str(obj)],obj,{**headers,**extra,str(source):digest(source)},console)
        cached([*common,'-target','x86_64-windows-gnu',*map(str,objects),'-o',str(program)],program,{str(p):digest(p) for p in objects},console)
        for name in ('pcm_stream','ogg_reader','stream_voice','scene_music'):
            source=ROOT/'engine'/f'{name}.cpp';arm=directory/f'{name}-aarch64.o';arms.append(arm)
            cached([*common,'-target','aarch64-linux-musl','-c',str(source),'-o',str(arm)],arm,{**headers,str(source):digest(source)},console)
        old=directory/'pcm-regression.exe'
        cached([*common,'-target','x86_64-windows-gnu',str(objects[0]),str(objects[1]),str(ROOT/'tests/pcm_audio.cpp'),'-o',str(old)],
            old,{str(p):digest(p) for p in [objects[0],objects[1],ROOT/'tests/pcm_audio.cpp']},console)
    with (ROOT/'logs/native-audio-pipeline-tests.log').open('wb') as console:
        run([str(program),str(ROOT/'res'),str(result)],console)
        old_result=directory/'pcm-regression.json';run([str(old),str(old_result)],console)
    for name,value in bindings.items():
        if digest(ROOT/name)!=value:raise ValueError('Native audio source changed: '+name)
    proof=json.loads(result.read_text());proof.update(source_sha256=bindings,
        pcm_regression=json.loads(old_result.read_text()),test_program_sha256=digest(program),
        arm64_objects_sha256={p.name:digest(p) for p in arms},arm64_target='aarch64-linux-musl',
        android_ndk_link_verified=False,android_device_verified=False,published_runtime_changed=False)
    (ROOT/'logs/native-audio-pipeline-verification.json').write_text(json.dumps(proof,indent=2)+'\n')
    print('PASS native audio pipeline:',proof['stream_cases'],'stream checks;',proof['music_cases'],'music checks; next scene command',
        proof['next_unimplemented_command'],proof['next_unimplemented_command_name'],'(not a playable APK).')


if __name__=='__main__':main()
