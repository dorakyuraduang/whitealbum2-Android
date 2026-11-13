"""Compare real static/native text callbacks at PC53 and PC64 in own RAM.

No game/device/window/PAK or original executable is opened. Single-tile runs
must use native glyphs throughout; mixed runs must retain exact static output
while non-admitted crossings take the original source path.
"""
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
from build_pak_tool import low_priority

ROOT=Path(__file__).resolve().parents[1]
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def main():
    low_priority();build=ROOT/'build/shared-aot-refresh-x64';folder=ROOT/'build/glyph-precision';folder.mkdir(exist_ok=True)
    original=ROOT/'tests/novel_cpu.cpp';fixture=original.read_text(encoding='utf-8')
    commands=json.loads((build/'compile_commands.json').read_text());command=next(x['command'] for x in commands if x['file'].replace('\\','/').endswith('/tests/novel_cpu.cpp'))
    if '"' in command:raise ValueError('Unquoted Windows workspace paths are required')
    block=(build/'build.ninja').read_text().split('build wa2_refresh_novel_cpu.exe:',1)[1].split('\n  LINK_LIBRARIES = ',1)[1].splitlines()[0];libraries=shlex.split(block)
    flags=subprocess.CREATE_NO_WINDOW|subprocess.BELOW_NORMAL_PRIORITY_CLASS
    env=os.environ.copy();env.update(ZIG_GLOBAL_CACHE_DIR=str(ROOT/'build/test-content-046/zig-cache'),ZIG_LOCAL_CACHE_DIR=str(ROOT/'build/zig-local'))
    results=[]
    with (folder/'check.log').open('wb') as log:
        def run(argv):subprocess.run(list(map(str,argv)),cwd=build,env=env,creationflags=flags,stdout=log,stderr=subprocess.STDOUT,timeout=180,check=True)
        for control in (0x27f,0x37f):
            for mixed in (False,True):
                text=fixture
                def change(old,new):
                    nonlocal text
                    if text.count(old)!=1:raise ValueError('Changed finite fixture anchor: '+old)
                    text=text.replace(old,new)
                change('p.cpu.fp.put16(0,0x7f)',f'p.cpu.fp.put16(0,0x{control:x})')
                if not mixed:
                    change("char('A'+n%26)","char('A'+n%5)")
                    change(r'{"ABC\nDEF",6}',r'{"ABC\nDEE",6}')
                    change('A^B~C`D','A^B^C`D')
                else:
                    change('CHECK(recorded.tiles==expected && recorded.calls.empty());','CHECK(recorded.tiles==expected);')
                    change('CHECK(after.accepted-before.accepted==expected_count && after.declined==before.declined);','CHECK(after.accepted>before.accepted && after.declined-before.declined==expected_count-(after.accepted-before.accepted));')
                    change("if(comma)report<<',';comma=true;",'const auto legacyDraws=recorded.calls.contains("DrawPrimitiveUP")?recorded.calls.at("DrawPrimitiveUP"):0;unsigned legacyStates{};for(const auto& [name,calls]:recorded.calls)if(name!="DrawPrimitiveUP")legacyStates+=calls;\n        if(comma)report<<\',\';comma=true;')
                    change(r'<<expected.size()<<",\"legacy_draws\":0,\"legacy_state_calls\":0,\"single_handle_reads\":"<<single_reads',r'<<expected.size()<<",\"legacy_draws\":"<<legacyDraws<<",\"legacy_state_calls\":"<<legacyStates<<",\"single_handle_reads\":"<<single_reads')
                text=text.replace('#include "../','#include "'+ROOT.as_posix()+'/')
                name=f'{control:x}-'+('mixed' if mixed else 'single');source=folder/(name+'.cpp');source.write_text(text,encoding='utf-8');obj=folder/(name+'.obj');binary=folder/(name+'.exe');report_path=folder/(name+'.json')
                args=command.split();args[args.index('-o')+1]=str(obj);args[-1]=str(source)
                run(args);run([ROOT/'tools/python/ziglang/zig.exe','c++','-target','x86_64-windows-gnu','-Os',obj,'-o',binary,*libraries]);run([binary,report_path])
                report=json.loads(report_path.read_text())
                if not report['passed'] or report['exact_full_text_cases']!=10:raise ValueError('Incomplete wider-precision text checks')
                if mixed and not report['pages'][1]['legacy_draws']:raise ValueError('Missing crossing fallback coverage')
                results.append(dict(control_word=control,mixed_crossings=mixed,exact_cases=10,derived_fixture_sha256=digest(source),binary_sha256=digest(binary),pages=report['pages']))
    sources=[Path(__file__),original,ROOT/'recompiled/native_sprites.cpp',ROOT/'recompiled/native_glyphs.hpp',ROOT/'engine/glyph_geometry_memo.hpp',ROOT/'scripts/prepare_shared_text_flow.py']
    result=dict(schema='wa2.glyph-precision-check.v1',source_sha256={p.relative_to(ROOT).as_posix():digest(p) for p in sources},shared_library_sha256=digest(build/'shared_aot/libwa2_shared_aot.a'),results=results,exact_full_text_cases=40,source_control_word_preserved=True,unsupported_crossings_use_source=True,game_launched=False,screen_access=False,source_executable_opened=False,passed=True)
    (ROOT/'logs/glyph-precision-check.json').write_text(json.dumps(result,indent=2)+'\n');print('PASS 40 static/native full text cases at PC53/PC64, including retained crossing fallbacks.')
if __name__=='__main__':main()
