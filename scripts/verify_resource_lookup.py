"""Compare lookup against the existing checked C++ in caller-owned RAM.

Only cached instruction templates and their cached initial bytes are read.
The reference is statically lowered to labels in a disposable test executable;
other callbacks use the existing shared Program. No EXE, decoder, original
resource, game entry, UI, graphics context or device is used.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
from build_pak_tool import low_priority

ROOT=Path(__file__).resolve().parents[1]

def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def derive():
    source=ROOT/'generated/wa2-win64/code_page_1112.cpp'
    pattern=re.compile(r'^case 0x([0-9a-f]+)u: \{ (.*?)\nreturn true; \}',re.S|re.M)
    rows={int(m[1],16):m[2] for m in pattern.finditer(source.read_text()) if 0x4588a0<=int(m[1],16)<0x4589ff}
    if min(rows)!=0x4588a0 or max(rows)!=0x4589fc or len(rows)<100:raise ValueError('Changed cached resource function extent')
    initial=bytes(map(int,re.findall(rb'\d+',(ROOT/'generated/static-data/text.inc').read_bytes())))
    base=int(re.search(rb'static_code_base=(\d+)u;',(ROOT/'generated/static-data/static_data.hpp').read_bytes())[1])
    parts=['#include "'+(ROOT/'recompiled/process.hpp').as_posix()+'"',
           '#include "'+(ROOT/'platform/compat/system.hpp').as_posix()+'"',
           '#include "'+(ROOT/'platform/compat/text.hpp').as_posix()+'"',
           '#include "'+(ROOT/'platform/compat/locale.hpp').as_posix()+'"',
           'static bool reference_lookup(wr::Cpu& s,wr::Memory& m){switch(s.eip){']
    parts += [f'case 0x{pc:x}u:goto L_{pc:x};' for pc in rows]
    parts += ['default:return false;}']
    direct_calls=set();indirect_calls=0
    for pc,body in rows.items():
        proof=re.match(r'wr::check_code\(m,s.eip,\{([\d,]+)\}\);\n',body)
        if not proof:raise ValueError('Missing cached byte proof')
        raw=bytes(map(int,proof[1].split(',')))
        if pc<base or initial[pc-base:pc-base+len(raw)]!=raw:raise ValueError('Cached template differs from initial constants')
        if 'wr::push(s,m,0x' in body and 's.eip=target;' in body:
            call=re.search(r'auto target=0x([0-9a-f]+)u;',body)
            if call:direct_calls.add(int(call[1],16))
            else:indirect_calls+=1
            tail='return true;'
        elif 's.eip=wr::pop(s,m)' in body:tail='return true;'
        else:
            targets=set(int(value,16) for value in re.findall(r'0x([0-9a-f]+)u',body[body.rfind('s.eip='):]))
            if 's.eip=target;' in body:
                target=re.search(r'auto target=0x([0-9a-f]+)u;s.eip=target;',body)
                if not target:raise ValueError('Unknown cached jump shape')
                targets={int(target[1],16)}
            if not targets or not targets<=rows.keys():raise ValueError('Cached local edge leaves the proved body')
            tail='\n'.join(f'if(s.eip==0x{target:x}u)goto L_{target:x};' for target in sorted(targets))+'\nthrow std::logic_error("unproved reference edge");'
        parts += [f'L_{pc:x}:{{',body,tail,'}']
    if direct_calls!={0x433f20,0x476194} or indirect_calls!=2:raise ValueError('Changed resource callback contract')
    parts += ['}',r'''
std::unique_ptr<wr::Process> make_resource_reference(bool linear){
    auto program=std::make_shared<std::unique_ptr<wa2::aot::Program>>();
    wr::ProcessOptions options;options.prefer_linear_memory=linear;
    auto process=std::make_unique<wr::Process>(wr::FunctionExecutor{[program](wr::Cpu& cpu,wr::Memory& memory){
        if(reference_lookup(cpu,memory))return true;
        return (*program)->invoke_unmetered(cpu);
    }},options);
    *program=std::make_unique<wa2::aot::Program>(process->memory);
    wa2::platform::compat::install_system(*process);
    wa2::platform::compat::install_text(*process);
    wa2::platform::compat::install_locale(*process);return process;
}
''']
    return '\n'.join(parts),len(rows)

def main():
    low_priority();build=ROOT/'build/shared-aot-refresh-x64';folder=ROOT/'build/resource-lookup-reference';folder.mkdir(exist_ok=True)
    text,locations=derive();source=folder/'reference.cpp';source.write_text(text,encoding='utf-8')
    commands=json.loads((build/'compile_commands.json').read_text())
    command=next(x['command'] for x in commands if x['file'].replace('\\','/').endswith('/tests/resource_lookup.cpp'))
    if '"' in command:raise ValueError('Unquoted Windows workspace paths are required')
    link=(build/'build.ninja').read_text().split('build wa2_refresh_resource_lookup.exe:',1)[1].split('\n  LINK_LIBRARIES = ',1)[1].splitlines()[0]
    libraries=shlex.split(link);flags=subprocess.CREATE_NO_WINDOW|subprocess.BELOW_NORMAL_PRIORITY_CLASS
    env=os.environ.copy();env.update(ZIG_GLOBAL_CACHE_DIR=str(ROOT/'build/test-content-046/zig-cache'),ZIG_LOCAL_CACHE_DIR=str(ROOT/'build/zig-local'))
    binary=folder/'reference.exe';reports=[]
    with (folder/'check.log').open('wb') as log:
        def run(argv):subprocess.run(list(map(str,argv)),cwd=build,env=env,creationflags=flags,stdout=log,stderr=subprocess.STDOUT,timeout=180,check=True)
        objects=[]
        for code in (ROOT/'tests/resource_lookup.cpp',source):
            args=command.split();args.insert(2,'-DWA2_RESOURCE_REFERENCE=1');obj=folder/(code.stem+'.obj');objects.append(obj)
            args[args.index('-o')+1]=str(obj);args[-1]=str(code);run(args)
        run([ROOT/'tools/python/ziglang/zig.exe','c++','-target','x86_64-windows-gnu','-Os',*objects,'-o',binary,*libraries])
        for name,exe in [('native',build/'wa2_refresh_resource_lookup.exe'),('reference',binary)]:
            path=folder/(name+'.json');run([exe,path]);reports.append(json.loads(path.read_text()))
        if any(not p['passed'] or p['queries']!=574 or p['diagnostics']!=116 or p['mutations']!=4 for p in reports):raise ValueError('Incomplete resource lookup cases')
        snapshots=[digest(folder/(name+'.json.snapshot')) for name in ('native','reference')]
        if snapshots[0]!=snapshots[1]:raise ValueError('Changed source directory results or callback ordering/text')
    inputs=[Path(__file__),ROOT/'tests/resource_lookup.cpp',ROOT/'port/native_resources.cpp',ROOT/'engine/resource_lookup.hpp',ROOT/'engine/resource_archive.cpp',ROOT/'generated/wa2-win64/code_page_1112.cpp',ROOT/'generated/static-data/text.inc',ROOT/'generated/static-data/static_data.hpp']
    result=dict(schema='wa2.resource-lookup-reference.v1',source_sha256={p.relative_to(ROOT).as_posix():digest(p) for p in inputs},checked_cached_locations=locations,query_cases=574,diagnostics=116,mutations=4,snapshot_sha256=snapshots[0],reference_binary_sha256=digest(binary),native_library_sha256=digest(build/'shared_aot/libwa2_function_process.a'),static_reference_labels=True,instruction_step_calls=0,source_executable_opened=False,original_resources_opened=False,game_launched=False,screen_access=False,passed=True)
    (ROOT/'logs/resource-lookup-reference.json').write_text(json.dumps(result,indent=2)+'\n');print('PASS 574 native/cached-source lookup cases and 116 diagnostic paths, exact result/text/order snapshots.')
if __name__=='__main__':main()
