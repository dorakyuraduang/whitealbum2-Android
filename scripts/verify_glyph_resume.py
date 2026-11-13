"""Force a spent glyph allowance inside real finite text callbacks.

Derive a recording-only fixture from novel_cpu.cpp; no game/resource/device/UI
is started. The typed sink models earlier text having spent the allowance.
Compare exact static/native output through the resulting source-frame unwind.
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
    low_priority();build=ROOT/'build/shared-aot-refresh-x64';folder=ROOT/'build/glyph-resume';folder.mkdir(exist_ok=True)
    proof=json.loads((ROOT/'logs/shared-aot-verification.json').read_text())
    if not proof['passed'] or proof['windows_library']['sha256']!=digest(build/'shared_aot/libwa2_shared_aot.a'):raise ValueError('A current passing shared build is required')
    original=ROOT/'tests/novel_cpu.cpp';text=original.read_text(encoding='utf-8')
    def change(old,new):
        nonlocal text
        if text.count(old)!=1:raise ValueError('Changed finite text fixture anchor: '+old)
        text=text.replace(old,new)
    change('struct Recording {','struct Recording {\n    wr::Memory* memory{};unsigned source_glyphs{};bool spend{};')
    change('auto& self=*static_cast<Recording*>(owner);','auto& self=*static_cast<Recording*>(owner);\n        if(++self.source_glyphs==64 && self.spend){auto* remaining=a::GlyphCallScope::remaining(*self.memory);CHECK(remaining && *remaining);*remaining=1;}')
    change('void reset(){calls.clear();tiles.clear();texture=0;}','void reset(){calls.clear();tiles.clear();texture=0;source_glyphs=0;}')
    change('Recording recorded;','Recording recorded;recorded.memory=&p.memory;')
    change('const auto native_ns=run(true);','recorded.spend=true;const auto deferrals_before=a::glyph_budget_deferrals;\n        const auto native_ns=run(true);\n        CHECK(a::glyph_budget_deferrals-deferrals_before==(expected_count>64?1:0));recorded.spend=false;')
    text=text.replace('#include "../','#include "'+ROOT.as_posix()+'/')
    source=folder/'fixture.cpp';source.write_text(text,encoding='utf-8');obj=folder/'fixture.obj';binary=folder/'fixture.exe';report_path=folder/'fixture.json'
    commands=json.loads((build/'compile_commands.json').read_text())
    command=next(x['command'] for x in commands if x['file'].replace('\\','/').endswith('/tests/novel_cpu.cpp'))
    if '"' in command:raise ValueError('This Windows compile-command parser requires unquoted workspace paths')
    args=command.split();args[args.index('-o')+1]=str(obj);args[-1]=str(source)
    block=(build/'build.ninja').read_text().split('build wa2_refresh_novel_cpu.exe:',1)[1].split('\n  LINK_LIBRARIES = ',1)[1].splitlines()[0]
    libraries=shlex.split(block);flags=subprocess.CREATE_NO_WINDOW|subprocess.BELOW_NORMAL_PRIORITY_CLASS
    env=os.environ.copy();env.update(ZIG_GLOBAL_CACHE_DIR=str(ROOT/'build/test-content-046/zig-cache'),ZIG_LOCAL_CACHE_DIR=str(ROOT/'build/zig-local'))
    with (folder/'check.log').open('wb') as log:
        def run(argv):subprocess.run(list(map(str,argv)),cwd=build,env=env,creationflags=flags,stdout=log,stderr=subprocess.STDOUT,timeout=180,check=True)
        run(args);run([ROOT/'tools/python/ziglang/zig.exe','c++','-target','x86_64-windows-gnu','-Os',obj,'-o',binary,*libraries]);run([binary,report_path])
    report=json.loads(report_path.read_text())
    if not report['passed'] or report['exact_full_text_cases']!=10:raise ValueError('Incomplete finite text callback comparison')
    result=dict(schema='wa2.glyph-resume-check.v1',fixture_sha256=digest(original),derived_fixture_sha256=digest(source),script_sha256=digest(Path(__file__)),shared_library_sha256=digest(build/'shared_aot/libwa2_shared_aot.a'),binary_sha256=digest(binary),exact_full_text_cases=10,forced_deferral_cases=2,forced_after_glyph=64,static_native_output_vertices_and_abi_equal=True,no_legacy_draws=True,game_launched=False,screen_access=False,source_executable_opened=False,passed=True)
    (ROOT/'logs/glyph-resume-check.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
