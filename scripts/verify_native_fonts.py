"""Verify native glyph indexing using existing generated code and cached typed constants."""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT=Path(__file__).resolve().parents[1]


def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    directory=ROOT/'build/native-fonts';directory.mkdir(parents=True,exist_ok=True)
    constants=json.loads((ROOT/'logs/native-font-constants.json').read_text())
    if digest(ROOT/'engine/font_ranges.inc')!=constants['generated_source_sha256']:raise ValueError('Font constants changed')
    fixture=directory/'font-ranges.bin'
    if digest(fixture)!=constants['fixture_sha256']:raise ValueError('Font fixture changed')
    signature=ROOT/'port/native_backend_data.inc';match=re.search(r'native_signature_1025=\{([^}]+)\}',signature.read_text())
    if not match:raise ValueError('Missing existing glyph-helper page')
    page=bytes(map(int,match.group(1).split(',')))
    if len(page)!=4096:raise ValueError('Invalid generated glyph-helper page')
    page_path=directory/'glyph-page.bin';page_path.write_bytes(page)
    zig=ROOT/'tools/python/ziglang/zig.exe';native=ROOT/'build/wa2-win64/winrecomp/libwinrecomp_native.a'
    sources=[ROOT/p for p in ('engine/font_glyphs.cpp','tests/native_fonts.cpp','tools/WindowsRecompTool-main/src/runtime/native_memory.cpp','generated/wa2-win64/code_page_1025.cpp')]
    bindings={p.relative_to(ROOT).as_posix():digest(p) for p in [*sources,ROOT/'engine/font_glyphs.hpp',ROOT/'engine/font_ranges.inc',signature,native,Path(__file__)]}
    environment=os.environ.copy();environment.update(ZIG_GLOBAL_CACHE_DIR=str(ROOT/'build/zig-cache'),ZIG_LOCAL_CACHE_DIR=str(ROOT/'build/zig-local'),TEMP=str(ROOT/'build/temp'),TMP=str(ROOT/'build/temp'))
    flags=(subprocess.BELOW_NORMAL_PRIORITY_CLASS|subprocess.CREATE_NO_WINDOW) if os.name=='nt' else 0
    common=[str(zig),'c++','-std=c++20','-O1','-g0','-target','x86_64-windows-gnu','-I'+str(ROOT/'engine'),'-I'+str(ROOT/'tools/WindowsRecompTool-main/include')]
    with (ROOT/'logs/native-fonts-build.log').open('wb') as console:
        objects=[]
        for i,source in enumerate(sources):
            obj=directory/(str(i)+'-'+source.stem+'.obj');objects.append(obj)
            subprocess.run([*common,'-c',str(source),'-o',str(obj)],cwd=ROOT,env=environment,creationflags=flags,stdout=console,stderr=subprocess.STDOUT,timeout=120,check=True)
        program=directory/'native-fonts-tests.exe'
        subprocess.run([*common,*map(str,objects),str(native),'-o',str(program)],cwd=ROOT,env=environment,creationflags=flags,stdout=console,stderr=subprocess.STDOUT,timeout=120,check=True)
        arm=directory/'font_glyphs-aarch64.o'
        subprocess.run([str(zig),'c++','-std=c++20','-O1','-g0','-target','aarch64-linux-musl','-I'+str(ROOT/'engine'),'-c',str(ROOT/'engine/font_glyphs.cpp'),'-o',str(arm)],cwd=ROOT,env=environment,creationflags=flags,stdout=console,stderr=subprocess.STDOUT,timeout=120,check=True)
    result=directory/'native-fonts-tests.json'
    with (ROOT/'logs/native-fonts-tests.log').open('wb') as console:
        subprocess.run([str(program),str(page_path),str(fixture),str(result)],cwd=ROOT,creationflags=flags,stdout=console,stderr=subprocess.STDOUT,timeout=30,check=True)
    for name,value in bindings.items():
        if digest(ROOT/name)!=value:raise ValueError('Glyph verification input changed: '+name)
    proof=json.loads(result.read_text());proof.update(source_sha256=bindings,constants_report_sha256=digest(ROOT/'logs/native-font-constants.json'),test_program_sha256=digest(program),
        arm64_target='aarch64-linux-musl',arm64_object_sha256=digest(arm),android_ndk_link_verified=False,apk_created=False,published_runtime_changed=False)
    (ROOT/'logs/native-fonts-verification.json').write_text(json.dumps(proof,indent=2)+'\n')
    print('PASS native glyph indexing:',proof['generated_code_comparisons'],'existing-code comparisons; no game or screen access.')


if __name__=='__main__':main()
