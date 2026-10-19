"""Build the native LSCR engine and compare with existing generated C++; no source EXE."""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT=Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    directory=ROOT/'build/native-script'
    directory.mkdir(parents=True,exist_ok=True)
    signatures=ROOT/'port/native_backend_data.inc'
    data=signatures.read_text()
    pages=[]
    for number in (1105,1106,1140):
        source=data if number!=1140 else (ROOT/'port/static_blocks_474.inc').read_text()
        prefix='native_signature_' if number!=1140 else 'page_signature_'
        match=re.search(prefix+str(number)+r'\s*=\s*\{([^}]+)\}',source)
        if not match:raise ValueError('Missing existing generated page '+str(number))
        page=bytes(map(int,match.group(1).split(',')))
        if len(page)!=4096:raise ValueError('Invalid generated page length')
        pages.append(page)
    fixture=directory/'legacy-pages.bin'
    fixture.write_bytes(b''.join(pages))
    zig=ROOT/'tools/python/ziglang/zig.exe'
    native=ROOT/'build/wa2-win64/winrecomp/libwinrecomp_native.a'
    sources=[ROOT/p for p in ('engine/script_runtime.cpp','engine/script_session.cpp','engine/scene_controls.cpp','engine/resource_archive.cpp','platform/file_io.cpp',
        'tests/native_script.cpp','tools/WindowsRecompTool-main/src/runtime/native_memory.cpp',
        'generated/wa2-win64/code_page_1105.cpp','generated/wa2-win64/code_page_1106.cpp','generated/wa2-win64/code_page_1140.cpp')]
    bindings={p.relative_to(ROOT).as_posix():digest(p) for p in [*sources,ROOT/'engine/script_runtime.hpp',native,signatures,ROOT/'port/static_blocks_474.inc',ROOT/'build/source-analysis/functions/00451a60.c']}
    environment=os.environ.copy()
    environment.update(ZIG_GLOBAL_CACHE_DIR=str(ROOT/'build/zig-cache'),ZIG_LOCAL_CACHE_DIR=str(ROOT/'build/zig-local'),TEMP=str(ROOT/'build/temp'),TMP=str(ROOT/'build/temp'))
    flags=(subprocess.BELOW_NORMAL_PRIORITY_CLASS|subprocess.CREATE_NO_WINDOW) if os.name=='nt' else 0
    common=[str(zig),'c++','-std=c++20','-O1','-g0','-ffp-contract=off','-target','x86_64-windows-gnu',
        '-I'+str(ROOT/'engine'),'-I'+str(ROOT/'platform'),'-I'+str(ROOT/'tools/WindowsRecompTool-main/include')]
    headers={p.relative_to(ROOT).as_posix():digest(p) for folder in ('engine','platform','tools/WindowsRecompTool-main/include/winrecomp') for p in (ROOT/folder).glob('*.hpp')}
    bindings.update(headers)
    bindings['scripts/verify_native_script.py']=digest(Path(__file__))
    def compile_cached(command,destination,inputs,console):
        stamp=destination.with_suffix(destination.suffix+'.json')
        identity=dict(command=command,inputs=inputs)
        if stamp.is_file() and destination.is_file():
            old=json.loads(stamp.read_text())
            if old.get('identity')==identity and old.get('sha256')==digest(destination):return
        subprocess.run(command,cwd=ROOT,env=environment,creationflags=flags,stdout=console,stderr=subprocess.STDOUT,timeout=120,check=True)
        stamp.write_text(json.dumps(dict(identity=identity,sha256=digest(destination)),indent=2)+'\n')
    objects=[]
    with (ROOT/'logs/native-script-build.log').open('wb') as console:
        for index,source in enumerate(sources):
            destination=directory/(str(index)+'-'+source.stem+'.obj');objects.append(destination)
            compile_cached([*common,'-c',str(source),'-o',str(destination)],destination,{**headers,str(source):digest(source)},console)
        program=directory/'native-script-tests.exe'
        compile_cached([*common,*map(str,objects),str(native),'-o',str(program)],program,{str(p):digest(p) for p in [*objects,native]},console)
        arms=[]
        for name in ('script_runtime','script_session','scene_controls'):
            arm=directory/(name+'-aarch64.o');arms.append(arm)
            compile_cached([str(zig),'c++','-std=c++20','-O1','-g0','-ffp-contract=off','-target','aarch64-linux-musl',
                '-I'+str(ROOT/'engine'),'-c',str(ROOT/'engine'/f'{name}.cpp'),'-o',str(arm)],arm,
                {**headers,f'engine/{name}.cpp':digest(ROOT/'engine'/f'{name}.cpp')},console)
    result=directory/'native-script-tests.json'
    with (ROOT/'logs/native-script-tests.log').open('wb') as console:
        subprocess.run([str(program),str(ROOT/'res'),str(fixture),str(result)],cwd=ROOT,
            creationflags=flags,stdout=console,stderr=subprocess.STDOUT,timeout=30,check=True)
    for name,value in bindings.items():
        if digest(ROOT/name)!=value:raise ValueError('Source changed during script verification: '+name)
    proof=json.loads(result.read_text())
    proof.update(source_sha256=bindings,test_program_sha256=digest(program),fixture_sha256=digest(fixture),
        script_archive_sha256=digest(ROOT/'res/ck-gal.pak'),script_bank='ck-gal.pak',arm64_objects_sha256={p.name:digest(p) for p in arms},
        arm64_target='aarch64-linux-musl',android_ndk_link_verified=False,apk_created=False,
        cpu_emulator_in_native_script_module=False,published_runtime_changed=False)
    (ROOT/'logs/native-script-verification.json').write_text(json.dumps(proof,indent=2)+'\n')
    print('PASS native script module:',proof['generated_code_numeric_comparisons'],'generated-code comparisons; real scene prefix parsed without CPU execution. Not a playable Android APK.')


if __name__=='__main__':
    main()
