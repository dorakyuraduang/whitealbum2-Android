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
ADVANCE_RANGES=(
    ('44751b','447539','f036c5e04efe00d6554885d674cc3ea877107c631f4e635c78b42af063c58825'),
    ('446939','446973','b734bf92f8a613fe1c1ea165b35d76a5ca357ac5270120f5ffe6e007c584e6d8'),
    ('44771c','447754','e4f983cf468da117c3efce9b27c7123d71c531c15bd205e848b3b58e63e079c3'),
)
LAYOUT_RANGES=(
    ('44722c','4472ea','6b8fae44c0972be7e10f955f3d928a915eb758a5d9919a68787a8e6797ca2944'),
    ('447321','4473b3','d54bba1510835e4caefc7172ce9c964d0ccfd03295e1d654e409f1c33d2b6dca'),
)
LAYOUT_FTOL=('474f00','474f36','8d56b0df360d361317d9764e221cd2b57a0a7fe47c98a40f4b3fba9dac7b36ae')
RUN_RANGES=(
    ('4473d8','447407','ce6dcab806716746b38e2da801313e3a64a17771f2deeeb048ebb108f3799d24'),
    ADVANCE_RANGES[0],ADVANCE_RANGES[1],ADVANCE_RANGES[2],
    ('446614','44671c','03c76c8858017ec0b0c16fa9afa2d848481cf666fc38e2df9cc6be9b5f99891f'),
    ('446782','4467bb','28e5303010b7895ce9c4b305adf417497c588c6160f3fda02e8428e9f7ee8044'),
    ('447216','44721e','72d4ba6078f5ea009e3b240e87f31accb23549a89a839b546833b10e55086e90'),
    ('44722c','447310','7de7a73b0b2f735c0ba69a74b5b3998d04ac9faea1ab8cfc399494873224cc16'),
    ('447321','4473d3','f2923807f7bcd2cb4371a3cb158d89a7448becef12f4de76c2371dda97404e6d'),
)
THUMBNAIL_RANGE=('43feb6','43fee4','5486efdd43457c32b2e87b2ff89e5639051ef872d9ff18acb2262ea7459147dc')
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

def verified_blocks(body,ranges):
    body=body.replace('\r\n','\n')
    parts=[]
    for start,end,expected in ranges:
        first=body.index('L_'+start+': {')
        last=body.index('L_'+end+': {',first)
        original=body[first:last]
        if digest(original.encode())!=expected:raise ValueError('Changed text layout/advance block '+start)
        parts.append(original)
    return ''.join(parts)

def advance_reference(body):
    """The complete original blocks; only the three terminal labels are supplied by the test."""
    return verified_blocks(body,ADVANCE_RANGES)

def layout_hooks(body,newline):
    verified_blocks(body,LAYOUT_RANGES)
    marker='TextAdvanceScope text_advance;'
    body=body.replace(marker,marker+newline+'TextLayoutReplay text_layout;')
    for label in ('44722c','4472ea','4473b3'):
        anchor=f'L_{label}: {{{newline}s.eip=0x{label}u;{newline}if constexpr(Metered)wr::tick(budget,s.eip);{newline}'
        if body.count(anchor)!=1:raise ValueError('Ambiguous text layout label '+label)
        if label=='44722c':
            hook=['if constexpr(!Metered)if(text_layout.enabled){',
                  '    floating.flush();integer.flush();',
                  '    switch(text_layout.begin(s,scope)){',
                  '    case 0x4472ea:goto L_4472ea;',
                  '    case 0x4473b3:goto L_4473b3;',
                  '    }','}']
        else:
            hook=['if constexpr(!Metered)if(text_layout.recording()){',
                  f'    floating.flush();integer.flush();text_layout.finish(s,scope,0x{label}u);','}']
        body=body.replace(anchor,anchor+newline.join(hook)+newline)
    return body

def run_hooks(body,newline):
    # Capture only paths inside this finite union. Each exit (markup, wrap,
    # output copy, extra glyph or return) cancels before its first instruction.
    # A prepared span also executes both original CALL boundaries and the
    # outline return's EBX reload. Bind the instructions the span replaces.
    verified_blocks(body,(
        ('447310','447321','1a682f4e703070c80eb55c16bf72b4bf92c8b4ac21aa06b724f452d640794ac3'),
        ('4473d3','4473d8','061276f3d9ca0e4b3223eebdc8cd1f2eee02bc08c5acbbb9d15f696b0d162442'),
    ))
    region=verified_blocks(body,RUN_RANGES)
    labels=set(re.findall(r'^L_([0-9a-f]+):',region,re.M))
    ends={'447310','4473d3'}
    exits=set(re.findall(r'goto L_([0-9a-f]+)',region))-labels-ends
    if exits!={'44671c','4467bb','446973','44721e','447407','44779d','4477cd'}:
        raise ValueError('Changed ordinary glyph run departures')
    marker='TextAdvanceScope text_advance;'
    body=body.replace(marker,marker+newline+'TextRunReplay text_run;'+newline+'TextPageReplay text_page(m);')
    for label in sorted(exits|ends|{'4473d8'}):
        anchor=f'L_{label}: {{{newline}s.eip=0x{label}u;{newline}if constexpr(Metered)wr::tick(budget,s.eip);{newline}'
        if body.count(anchor)!=1:raise ValueError('Ambiguous glyph run label '+label)
        if label=='4473d8':
            hook=['if constexpr(!Metered)if(text_page.enabled()){',
                  '    floating.flush();integer.flush();',
                  '    switch(text_page.begin(s,scope)){',
                  '    case 0x4473d8:goto L_4473d8;',
                  '    case 0x447310:goto L_447310;',
                  '    case 0x4473d3:goto L_4473d3;',
                  '    }','}',
                  'if constexpr(!Metered)if(text_run.enabled){',
                  '    floating.flush();integer.flush();',
                  '    switch(text_run.begin(s,scope)){',
                  '    case 0x447310:goto L_447310;',
                  '    case 0x4473d3:goto L_4473d3;',
                  '    }','}']
        elif label in ends:
            hook=['if constexpr(!Metered)if(text_page.recording()){',
                  f'    floating.flush();integer.flush();text_page.finish(s,scope,0x{label}u);','}',
                  'if constexpr(!Metered)if(text_run.recording()){',
                  f'    floating.flush();integer.flush();text_run.finish(s,scope,0x{label}u);','}']
        else:
            hook=['if constexpr(!Metered){text_run.cancel();text_page.cancel();}']
        body=body.replace(anchor,anchor+newline.join(hook)+newline)
    return body

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
    body=body.replace(declaration,declaration+newline+'TextJumpBudget<Metered> local_text_jumps;')
    advance_reference(body)
    body=body.replace('TextJumpBudget<Metered> local_text_jumps;',
                      'TextJumpBudget<Metered> local_text_jumps;'+newline+'TextAdvanceScope text_advance;')
    body=run_hooks(body,newline)
    # The run hooks fall exactly at the CALLs, outside the layout slices.
    body=layout_hooks(body,newline)
    anchor=f'L_44751b: {{{newline}s.eip=0x44751bu;{newline}if constexpr(Metered)wr::tick(budget,s.eip);{newline}'
    if body.count(anchor)!=1:raise ValueError('Ambiguous ordinary text advance entry')
    hook=['if constexpr(!Metered)if(text_advance.enabled){',
          '    switch(compact_text_advance(s,scope,integer)){',
          '    case 0x446973: ++text_advance.hits;goto L_446973;',
          '    case 0x44779d: ++text_advance.hits;goto L_44779d;',
          '    case 0x446614: ++text_advance.hits;goto L_446614;',
          '    }','}']
    body=body.replace(anchor,anchor+newline.join(hook)+newline)
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
    return '#include "text_page_replay.hpp"'+newline+'#include "text_advance.hpp"'+newline+'#include "text_jump_budget.hpp"'+newline+text[:begin]+body+text[end:],sorted(targets)

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
    advance=ROOT/'recompiled/text_advance.hpp'
    inputs[advance.relative_to(ROOT).as_posix()]=digest(advance.read_bytes())
    for name in ('save_thumbnail.hpp','text_layout_replay.hpp','text_layout_replay_control.hpp','text_run_replay.hpp','text_run_replay_control.hpp',
                 'text_page_packet.hpp','text_page_replay.hpp','text_page_replay_control.hpp'):
        header=ROOT/'recompiled'/name
        inputs[header.relative_to(ROOT).as_posix()]=digest(header.read_bytes())
    reference=output.parent/'text_advance_reference.inc'
    payload=advance_reference(source.read_text(encoding='utf-8')).encode()
    write(reference,payload);outputs[reference.relative_to(ROOT).as_posix()]=digest(payload)
    for name,entry in HELPERS.items():
        path=ROOT/'generated/shared-aot'/name;raw=path.read_bytes();inputs[path.relative_to(ROOT).as_posix()]=digest(raw)
        original=raw.decode('utf-8');newline='\r\n' if '\r\n' in original else '\n'
        if entry=='4462e0':
            verified_blocks(original,(LAYOUT_FTOL,))
            hook='if constexpr(!Metered)if(wa2::aot::native_draw_glyph(s,m))return;'
            if original.count(hook)!=1:raise ValueError('Changed cached glyph preheader')
            original=original.replace(hook,hook.replace('native_draw_glyph(','native_draw_glyph_or_yield('))
        begin=original.index(f'template<bool Metered> void f_{entry}(')
        marker=f'template void f_{entry}<false>(Context&,wr::Memory&,Scope&,std::uint64_t&,std::uint32_t);'
        if entry=='445350':
            end=original.index(marker,begin)
            if digest(original[begin:end].replace('\r\n','\n').encode())!='185fba09109524b12b54797394fe432990faaae68ce1ee7712630c278fe4332e':
                raise ValueError('Changed text speed accumulator read/write contract')
        end=original.index(marker,begin)+len(marker);body=original[begin:end]
        # This is the same checked body, isolated for O2. Its stack is already
        # protected by the passed scope; only the redundant TLS/lock lookup is
        # removed, not range/permission checks or architectural stores.
        body,count=scoped_stack(body)
        stack_sites+=count;helper_bodies.append(body.replace('\r\n','\n'))
        rest=original[:begin]+original[end:]
        if name=='native/functions_2.cpp':
            pixel_loop=verified_blocks(rest,(THUMBNAIL_RANGE,))
            reference=output.parent/'save_thumbnail_reference.inc'
            payload=pixel_loop.encode('utf-8');write(reference,payload)
            outputs[reference.relative_to(ROOT).as_posix()]=digest(payload)
            anchor=f'L_43feb6: {{{newline}s.eip=0x43feb6u;{newline}'
            if rest.count(anchor)!=1:raise ValueError('Changed cached save thumbnail entry')
            rest='#include "save_thumbnail.hpp"'+newline+rest.replace(anchor,anchor+
                'if constexpr(!Metered)if(copy_save_thumbnail(s,m,scope))goto L_43fec0;'+newline)
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
