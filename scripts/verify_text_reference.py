"""Compare native text geometry to existing generated C++, without a source EXE."""
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
from cached_game_data import Constants

ROOT=Path(__file__).resolve().parents[1]


def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    ctypes.windll.kernel32.SetPriorityClass(ctypes.windll.kernel32.GetCurrentProcess(),0x4000)
    build=ROOT/'build/native-text-reference';build.mkdir(parents=True,exist_ok=True)
    sources=[ROOT/'generated/wa2-win64'/('code_page_%d.cpp'%number) for number in (1093,1094,1095,1140)]
    pages=[];existing_pages={}
    for source in sources:
        matches=list(re.finditer(r'case 0x([0-9a-f]+)u: \{ wr::check_code\(m,s.eip,\{([^}]+)\}\);',source.read_text()))
        for match in matches:
            address=int(match[1],16);instruction=bytes(map(int,match[2].split(',')))
            for offset,value in enumerate(instruction):
                at=address+offset;contents=existing_pages.setdefault(at&~4095,bytearray([0xcc]*4096));contents[at&4095]=value
    pages +=[(address,5,bytes(contents)) for address,contents in sorted(existing_pages.items())]
    # Generated functions jump through in-page constant tables. Reuse the full
    # already-generated page, including those table bytes; never open an EXE.
    existing=(ROOT/'port/native_backend_data.inc').read_text()
    helper=(ROOT/'port/static_blocks_474.inc').read_text()
    for index,(address,permissions,contents) in enumerate(pages):
        number=address>>12;source=helper if number==1140 else existing
        name='page_signature_' if number==1140 else 'native_signature_'
        match=re.search(name+str(number)+r'\s*=\s*\{([^}]+)\}',source)
        if match:
            contents=bytes(map(int,match[1].split(',')))
            if len(contents)!=4096:raise ValueError('Invalid existing text reference page')
            pages[index]=(address,permissions,contents)
    constants=Constants()
    for address in (0x4a1000,0x4a2000,0x4a3000,0x4a5000,0x4c2000):pages.append((address,3,constants.read(address,4096)))
    fixture=build/'cached-reference.bin';fixture.write_bytes(struct.pack('<I',len(pages))+b''.join(struct.pack('<III',a,len(b),p)+b for a,p,b in pages))
    sources +=[ROOT/'tests/text_reference.cpp',ROOT/'tools/WindowsRecompTool-main/src/runtime/native_memory.cpp']
    libraries=[ROOT/'build/native-canvas/native_engine/libwa2_engine.a',ROOT/'build/wa2-win64/winrecomp/libwinrecomp_native.a']
    bindings={p.relative_to(ROOT).as_posix():digest(p) for p in [*sources,*libraries,Path(__file__),ROOT/'port/native_backend_data.inc',ROOT/'port/static_blocks_474.inc']}
    headers={p.relative_to(ROOT).as_posix():digest(p) for folder in ('engine','tools/WindowsRecompTool-main/include/winrecomp') for p in (ROOT/folder).glob('*.hpp')}
    environment=os.environ.copy();environment.update(ZIG_GLOBAL_CACHE_DIR=str(ROOT/'build/zig-cache'),ZIG_LOCAL_CACHE_DIR=str(ROOT/'build/zig-local'),TEMP=str(ROOT/'build/temp'),TMP=str(ROOT/'build/temp'))
    flags=subprocess.BELOW_NORMAL_PRIORITY_CLASS|subprocess.CREATE_NO_WINDOW
    common=[str(ROOT/'tools/python/ziglang/zig.exe'),'c++','-std=c++20','-O1','-g0','-target','x86_64-windows-gnu','-I'+str(ROOT/'engine'),'-I'+str(ROOT/'tools/WindowsRecompTool-main/include')]
    with (ROOT/'logs/native-text-reference-build.log').open('wb') as console:
        objects=[]
        for index,source in enumerate(sources):
            obj=build/(str(index)+'-'+source.stem+'.obj');objects.append(obj)
            identity=dict(command=common,source=digest(source),headers=headers);stamp=obj.with_suffix('.json')
            if not obj.is_file() or not stamp.is_file() or json.loads(stamp.read_text())!=identity:
                subprocess.run([*common,'-c',str(source),'-o',str(obj)],cwd=ROOT,env=environment,creationflags=flags,stdout=console,stderr=subprocess.STDOUT,timeout=120,check=True)
                stamp.write_text(json.dumps(identity,indent=2)+'\n')
        program=build/'text-reference.exe'
        subprocess.run([*common,*map(str,objects),*map(str,libraries),'-o',str(program)],cwd=ROOT,env=environment,creationflags=flags,stdout=console,stderr=subprocess.STDOUT,timeout=120,check=True)
    report=build/'text-reference.json'
    with (ROOT/'logs/native-text-reference-tests.log').open('wb') as console:
        subprocess.run([str(program),str(fixture),str(report)],cwd=ROOT,creationflags=flags,stdout=console,stderr=subprocess.STDOUT,timeout=10,check=True)
    for name,value in bindings.items():
        if digest(ROOT/name)!=value:raise ValueError('Text reference source changed: '+name)
    proof=json.loads(report.read_text());proof.update(source_sha256=bindings,header_sha256=headers,fixture_sha256=digest(fixture),test_program_sha256=digest(program),
        production_cpu_emulator_linked=False,android_device_verified=False)
    (ROOT/'logs/native-text-reference-verification.json').write_text(json.dumps(proof,indent=2)+'\n')
    print('PASS native glyph geometry against existing generated C++; no source EXE or screen access.')


if __name__=='__main__':main()
