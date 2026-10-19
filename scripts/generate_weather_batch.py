"""Compile one bounded weather region and its admitted callees from cached C++.

No executable is opened. Every call/return/branch remains statically selected.
Calls outside the list stop before the original call, committing its exact prefix.
"""
import hashlib
import json
import re

ENTRIES=(0x4088cf,0x408920)
ENDS={0x408f5b,0x409c60}
CALLEES={0x449860,0x4397e0,0x449870,0x475430,0x449950,0x449980,
         0x449d10,0x449d20,0x449d40,0x449d70,0x449d80,0x449a00,0x449a30,
         0x43a7d0,0x43a850,0x474f00,0x437db0,0x47e778,0x47e7cf,0x43b290}


def flag_elisions(g,functions,bodies,edges,yields):
    """Interprocedural raw/defined flag liveness inside one atomic prefix.

    Local memory faults decline the transaction, so only the explicit prefix
    exits observe flags. Callees share this same private register state.
    """
    returns={entry:set() for entry in functions};nodes=set().union(*map(set,functions.values()))
    for pc in nodes:
        if pc in yields:continue
        for edge in edges[pc]:
            if edge['kind']=='call_direct':
                target=edge['to'];continuation,={e['to'] for e in edges[pc] if e['kind']=='call_continuation'}
                returns[target].add(continuation)
    successors={pc:set() for pc in nodes}
    for entry,locations in functions.items():
        for pc in locations:
            if pc in yields:continue
            kinds={e['kind'] for e in edges[pc]}
            if 'return' in kinds:successors[pc]|=returns[entry]
            elif 'call_direct' in kinds:successors[pc]|={e['to'] for e in edges[pc] if e['kind']=='call_direct'}
            else:successors[pc]|={e['to'] for e in edges[pc] if e['to'] is not None and e['kind']!='data_callback_initial'}
    effects={}
    neutral={'reg','set_reg','mask','sign_extend','push','pop','string_op','x87_memory','x87_execute',
        'sse_memory','sse_execute','sse_sd_memory_to_i32','multiply','divide','bit_op','bit_address','scan','segment_address'}|set(g.FLAG_WRITES)
    for pc in nodes:
        plain=bodies[pc][0].replace('s.flags=f.flags;s.defined_flags=f.defined_flags;','')
        operations=re.findall(r'wr::(add|sub|logic|incdec|imul)\(f,',plain)
        operation=operations[0] if len(operations)==1 else None;reads=0
        stripped=plain
        for mask in re.finditer(r's\.flags&((?:wr::(?:CF|PF|AF|ZF|SF|OF))(?:\|wr::(?:CF|PF|AF|ZF|SF|OF))*)',plain):
            for name in re.findall(r'wr::(CF|PF|AF|ZF|SF|OF)',mask[1]):reads|=g.FLAG_BITS[name]
            stripped=stripped.replace(mask[0],'known_flag_operand')
        if 's.flags' in stripped or 's.defined_flags' in stripped:reads=g.ALL_FLAGS
        helpers=set(re.findall(r'\bwr::(\w+)\(',plain))
        if 'shift' in helpers:
            # Non-carry shifts have no flag input in their result. Keeping
            # their writes conservative still propagates every live old bit.
            if not re.search(r'wr::shift\(f,[^;]+,(?:0|1|2)\)',plain):reads|=g.ALL_FLAGS
            helpers.remove('shift')
        if helpers-neutral:reads|=g.ALL_FLAGS
        effects[pc]=(reads,g.FLAG_WRITES.get(operation,0),operation)
    live={pc:g.ALL_FLAGS if pc in yields else 0 for pc in nodes};live.update({pc:g.ALL_FLAGS for pc in ENDS})
    changed=True
    while changed:
        changed=False
        for pc in sorted(nodes,reverse=True):
            if pc in yields:continue
            out=0
            for target in successors[pc]:out|=live.get(target,g.ALL_FLAGS)
            reads,writes,_=effects[pc];value=reads|(out&~writes)
            if live[pc]!=value:live[pc]=value;changed=True
    result={}
    for pc in nodes:
        if pc in yields:continue
        out=0
        for target in successors[pc]:out|=live.get(target,g.ALL_FLAGS)
        _,writes,operation=effects[pc]
        if operation and not out&writes:result[pc]=operation
    return result


def generate(bodies,edges,owners,positions,instructions):
    import generate_shared_aot as g
    rows={row['address']:row for row in instructions};functions={};yields=set();pending=list(ENTRIES[:1])
    while pending:
        entry=pending.pop()
        if entry in functions:continue
        root=entry==ENTRIES[0];todo=list(ENTRIES) if root else [entry];seen=set()
        while todo:
            pc=todo.pop()
            if pc in seen or (root and pc in ENDS):continue
            if pc not in bodies:raise ValueError('Missing cached weather location: '+hex(pc))
            if root and not 0x4088cf<=pc<0x408f5b:raise ValueError('Weather region escaped: '+hex(pc))
            seen.add(pc);following=edges[pc];kinds={e['kind'] for e in following}
            if kinds&{'call_import','jump_import','call_indirect','stop'} or g.PENDING_TRANSCENDENTAL.search(bodies[pc][0]):
                yields.add(pc);continue
            if 'call_direct' in kinds:
                target,={e['to'] for e in following if e['kind']=='call_direct'}
                if target not in CALLEES:yields.add(pc);continue
                pending.append(target)
                todo.extend(e['to'] for e in following if e['kind']=='call_continuation')
            elif 'return' not in kinds:
                if 'jump_indirect' in kinds and not any(e['kind']=='jump_table_static' for e in following):
                    yields.add(pc);continue
                todo.extend(e['to'] for e in following if e['to'] is not None and e['kind']!='data_callback_initial')
        functions[entry]=sorted(seen)
    color_nodes=[pc for pc in functions[0x43b290] if 0x43b290<=pc<=0x43b3be]
    color_hash=hashlib.sha256(b''.join(bodies[pc][1] for pc in color_nodes)).hexdigest()
    if len(color_nodes)!=101 or color_hash!='8479b6d4e1c4fce17247dea97c65da581c5822540bcca4febcabfc8a6f27b53f':
        raise ValueError('Cached source of the typed weather color replacement changed')
    dead_flags=flag_elisions(g,functions,bodies,edges,yields)
    output=['// Generated only from the checked cached C++/CFG; common to all hosts.']
    for entry in sorted(functions):
        output.append(f'[[gnu::noinline]] void weather_{entry:x}(wr::Cpu&,WeatherFrame&,WeatherFloating&);')
    for entry,nodes in sorted(functions.items()):
        root=entry==ENTRIES[0]
        output += [f'[[gnu::noinline]] void weather_{entry:x}(wr::Cpu& s,WeatherFrame& scope,WeatherFloating& floating){{']
        if entry==0x43b290:output.append('if(weather_color_direct(s,scope))return;')
        if root:output.append(f'if(s.eip==0x{ENTRIES[1]:x}u)goto L_{ENTRIES[1]:x};')
        output.append(f'goto L_{entry:x};')
        for pc in nodes:
            output += [f'L_{pc:x}: {{',f's.eip=0x{pc:x}u;']
            if root and pc in (0x4088d1,0x408920,0x408934):output.append('scope.iteration();')
            kinds={e['kind'] for e in edges[pc]}
            if pc in yields:body='throw WeatherYield{};'
            elif 'call_direct' in kinds:
                target,={e['to'] for e in edges[pc] if e['kind']=='call_direct'}
                continuation,={e['to'] for e in edges[pc] if e['kind']=='call_continuation'}
                body=(bodies[pc][0]+f'\nweather_{target:x}(s,scope,floating);\n'
                      f'if(s.eip!=0x{continuation:x}u)throw WeatherDeclined{{}};\ngoto L_{continuation:x};')
            else:body=g.emit_body(pc,bodies[pc][0],edges,owners,positions)
            if pc in dead_flags:
                operation=dead_flags[pc];body=body.replace('wr::'+operation+'(',f'wa2::aot::value::{operation}<false>(')
            body=g.fixed_memory(g.portable_floating(body))
            # The x87 register file spans the complete region and every direct
            # callee. SIMD state remains in Cpu; it cannot observe that file.
            body=re.sub(r'wa2::aot::fp::x87_memory<wa2::aot::fp::x87<([^>]+)>>\(s,m,',r'floating.template memory<\1>(',body)
            body=re.sub(r'wa2::aot::fp::x87_execute<wa2::aot::fp::x87<([^>]+)>>\(s,',r'floating.template execute<\1>(',body)
            body=body.replace('s.fp.get16(0)','floating.word(0)').replace('s.fp.get16(2)','floating.word(2)')
            body=body.replace('wr::push(s,m,','weather_push(s,scope,').replace('wr::pop(s,m','weather_pop(s,scope')
            body=body.replace('wr::string_op(s,m,','weather_string(s,scope,')
            body=re.sub(r'(weather_string\([^;]+),budget\);',r'\1);',body)
            body=body.replace('wa2::aot::fp::sse_memory<','weather_sse_memory<').replace('wa2::aot::fp::sse_sd_memory_to_i32<','weather_sse_to_i32<')
            body=body.replace('>(s,m,','>(s,scope,')
            if 'm.' in body or re.search(r'\bm\b|\bbudget\b|f_[0-9a-f]+<Metered>|wr_(?:x87|sse)_',body):
                raise ValueError('Unbridged batch state consumer at '+hex(pc)+': '+body)
            # Keep the already proved vector clear as one local buffer write.
            if pc==0x47e798:body='weather_vector_clear(s,scope);\ngoto L_47e7c8;'
            output += [body,'}']
        if root:
            output += [f'L_{pc:x}: s.eip=0x{pc:x}u;return;' for pc in sorted(ENDS)]
        output += ['}']
    g.write('weather_batch.inc','\n'.join(output)+'\n')
    own=[pc for pc in sorted(ENDS|yields) if owners[pc]==owners[ENTRIES[0]]]
    external=[pc for pc in sorted(yields) if owners[pc]!=owners[ENTRIES[0]]]
    g.write('weather_batch.json',json.dumps(dict(entries=ENTRIES,normal_ends=sorted(ENDS),
        own_exit_targets=own,external_exit_targets=external,callee_entries=sorted(functions.keys()-{ENTRIES[0]}),
        cached_locations=sum(map(len,functions.values())),elided_flag_calculations=len(dead_flags),
        native_color=dict(entry=0x43b290,last_source_location=0x43b3be,cached_locations=len(color_nodes),
            cached_bytes_sha256=color_hash,implementation='recompiled/native_weather.cpp',
            domain='index < 2048; corner < 4; RGB <= 255; signed 32-bit alpha'),
        source='cached checked C++/CFG only'),indent=2)+'\n')
    return own,external
