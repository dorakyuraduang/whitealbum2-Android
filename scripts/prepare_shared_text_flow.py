"""Flatten proved local text JMPs from cached C++; never opens an executable.

The three existing indirect JMP sites use their initial table
destinations come from cached initial-data constants and must name labels of
the same function. Other destinations keep the original checked dispatcher.
Six existing glyph/text helpers are isolated for optimization; their checked
32-bit stack operations reuse the scope already held by the caller.
The glyph entry yields on a spent typed allowance before using a slow body.
"""
import hashlib
import json
from pathlib import Path
import re
import struct
from prepare_shared_language import prepare as prepare_language
from prepare_shared_chroma import prepare as prepare_chroma
from prepare_shared_progress import prepare as prepare_progress

ROOT=Path(__file__).resolve().parents[1]
TABLES=((0x44788c,10),(0x447934,4),(0x447988,10))
SITES=(0x446723,0x446868,0x4468fd)
HELPERS={'native/functions_2.cpp':'4452f0','functions_3.cpp':'445350','functions_6.cpp':'4775f1','native-progress/functions_7.cpp':'477604','native-progress/functions_0.cpp':'401050','functions_5.cpp':'4462e0'}
def digest(data):return hashlib.sha256(data).hexdigest()
def write(path,data):
    path.parent.mkdir(parents=True,exist_ok=True)
    if not path.exists() or path.read_bytes()!=data:path.write_bytes(data)

def scoped_stack(body):
    # Selected cached bodies push only a previously evaluated word or a literal.
    # Reject any changed width/operand rather than interpreting a new shape.
    body,pushes=re.subn(r'wr::push\(s,m,(value|0x[0-9a-f]+u)(?:,32)?\);',r'text_push(s,scope,\1);',body)
    body,pops=re.subn(r'wr::pop\(s,m(?:,32)?\)',r'text_pop(s,scope)',body)
    if not pushes or not pops or 'wr::push(' in body or 'wr::pop(' in body:
        raise ValueError('Changed selected text stack contract')
    return body,pushes+pops

def derive(text,code,base):
    newline='\r\n' if '\r\n' in text else '\n'
    begin=text.index('template<bool Metered,bool NativeRegisters> void f_446500(')
    end=text.index('template<bool Metered,bool NativeRegisters> void f_4492e0(',begin)
    body=text[begin:end]
    labels=set(int(x,16) for x in re.findall(r'^L_([0-9a-f]+):',body,re.M))
    targets=set()
    for address,count in TABLES:
        offset=address-base
        if offset<0 or offset+count*4>len(code):raise ValueError('Cached text table is outside initial constants')
        values=struct.unpack_from('<'+'I'*count,code,offset)
        if not set(values)<=labels:raise ValueError('Cached text JMP leaves its owning function')
        targets.update(values)
    declaration='wa2::aot::IntegerRegisters<Metered || !NativeRegisters> integer(s,m,scope);'
    if body.count(declaration)!=1:raise ValueError('Ambiguous text register scope')
    body=body.replace(declaration,declaration+newline+'TextJumpBudget<Metered> local_text_jumps(resume,s.fp.get16(0));')
    old='if constexpr(!Metered)(void)floating.external([&]{return integer.external([&]{return call_source_indirect(s,m,scope);});});'
    if body.count(old)!=len(SITES):raise ValueError('Changed cached text indirect-transfer set')
    for site in SITES:
        start=body.index(f'L_{site:x}: {{{newline}')
        finish=body.index(newline+'}',start)
        fragment=body[start:finish]
        if fragment.count(old)!=1 or 's.eip=target;' not in fragment or 'push<32>' in fragment:raise ValueError('Site is not the expected source JMP')
        body=body[:start]+fragment.replace(old,'if constexpr(!Metered)goto L_text_local_dispatch;')+body[finish:]
    # A shared native switch branches to the existing source labels. No new
    # target is admitted; external/modified destinations retain the old path.
    marker=body.index('#if WA2_AOT_DIAGNOSTICS')
    finish=body.rfind(newline+'}'+newline,0,marker)
    if finish<0:raise ValueError('Missing text function end')
    dispatch=['L_text_local_dispatch: {','switch(s.eip){']
    dispatch += [f'case 0x{target:x}u: if(!local_text_jumps.take())return;goto L_{target:x};' for target in sorted(targets)]
    dispatch += ['default:local_text_jumps.external();',old,'return;','}','}']
    body=body[:finish]+newline+newline.join(dispatch)+body[finish:]
    return '#include "text_jump_budget.hpp"'+newline+text[:begin]+body+text[end:],sorted(targets)

def prepare():
    prepare_chroma();prepare_language();prepare_progress()
    source=ROOT/'generated/shared-aot/native-language/functions_hot.cpp'
    constants=ROOT/'generated/static-data/text.inc';header=ROOT/'generated/static-data/static_data.hpp'
    code=bytes(map(int,re.findall(rb'\d+',constants.read_bytes())))
    base=int(re.search(rb'static_code_base=(\d+)u;',header.read_bytes())[1])
    size=int(re.search(rb'static_code_bytes\[(\d+)\]',header.read_bytes())[1])
    if len(code)!=size:raise ValueError('Incomplete cached initial text constants')
    text,targets=derive(source.read_text(encoding='utf-8'),code,base)
    output=ROOT/'generated/shared-aot/native-text/functions_hot.cpp';data=text.encode('utf-8');write(output,data)
    inputs={p.relative_to(ROOT).as_posix():digest(p.read_bytes()) for p in (source,constants,header,Path(__file__),ROOT/'recompiled/text_jump_budget.hpp',ROOT/'recompiled/text_stack.hpp',ROOT/'recompiled/text_ftol.hpp',ROOT/'recompiled/text_ftol.cpp')}
    outputs={output.relative_to(ROOT).as_posix():digest(data)};helper_bodies=[];stack_sites=0
    for name,entry in HELPERS.items():
        path=ROOT/'generated/shared-aot'/name;raw=path.read_bytes();inputs[path.relative_to(ROOT).as_posix()]=digest(raw)
        original=raw.decode('utf-8');newline='\r\n' if '\r\n' in original else '\n'
        if entry=='4462e0':
            hook='if constexpr(!Metered)if(wa2::aot::native_draw_glyph(s,m))return;'
            if original.count(hook)!=1:raise ValueError('Changed cached glyph preheader')
            original=original.replace(hook,hook.replace('native_draw_glyph(','native_draw_glyph_or_yield('))
        begin=original.index(f'template<bool Metered> void f_{entry}(')
        marker=f'template void f_{entry}<false>(Context&,wr::Memory&,Scope&,std::uint64_t&,std::uint32_t);'
        end=original.index(marker,begin)+len(marker);body=original[begin:end]
        # This is the same checked body, isolated for O2. Its stack is already
        # protected by the passed scope; only the redundant TLS/lock lookup is
        # removed, not range/permission checks or architectural stores.
        body,count=scoped_stack(body)
        stack_sites+=count;helper_bodies.append(body.replace('\r\n','\n'))
        rest=original[:begin]+original[end:]
        if entry=='4462e0':
            anchor=f'L_474f00: {{{newline}s.eip=0x474f00u;{newline}if constexpr(Metered)wr::tick(budget,s.eip);{newline}'
            if rest.count(anchor)!=1:raise ValueError('Changed cached integer-conversion entry')
            rest='#include "text_ftol.hpp"'+newline+rest.replace(anchor,anchor+'if constexpr(!Metered)if(native_integer_ftol(s,m,scope))return;'+newline)
        if '/' not in name:
            rest=rest.replace('#include "bodies.hpp"','#include "../bodies.hpp"').replace('#include "../../','#include "../../../')
        target=output.parent/path.name;payload=rest.encode('utf-8');write(target,payload);outputs[target.relative_to(ROOT).as_posix()]=digest(payload)
    helpers='#include "../bodies.hpp"\n#include "text_stack.hpp"\n#include "native_glyphs.hpp"\nnamespace wa2::aot {\n'+'\n'.join(helper_bodies)+'\n}\n'
    target=output.parent/'text_helpers.cpp';payload=helpers.encode('utf-8');write(target,payload);outputs[target.relative_to(ROOT).as_posix()]=digest(payload)
    metadata=dict(schema='wa2.derived-text-flow.v1',inputs=inputs,outputs=outputs,local_jmp_sites=[hex(x) for x in SITES],local_targets=[hex(x) for x in targets],hot_helpers=list(HELPERS.values()),scoped_stack_sites=stack_sites,glyph_entry_defers_on_exhaustion=True,integer_conversion_entry='0x474f00',cached_inputs_unchanged=True,source_executable_opened=False)
    write(output.parent/'provenance.json',(json.dumps(metadata,indent=2)+'\n').encode());return metadata
if __name__=='__main__':print(json.dumps(prepare(),indent=2))
