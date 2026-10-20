"""Lift the existing checked C++/CFG into one portable set of native functions.

No EXE, decoder, external game code, host assembler, or platform-specific game
logic is used. Static calls stay native. Imports and dynamic calls unwind to
the host and resume at compiled labels; unknown destinations still fail.
Overlapping CFG functions share one body instead of duplicating their tails.
Deferred functions remain explicit; this is not a whole-game coverage claim.
"""
from __future__ import annotations
from collections import Counter, defaultdict, deque
import hashlib
import json
import os
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'generated/shared-aot'
CASE = re.compile(r'^case 0x([0-9a-f]+)u: \{ (.*?)\nreturn true; \}', re.S | re.M)
PROOF = re.compile(r'^wr::check_code\(m,s.eip,\{([\d,]+)\}\);\n')
HELPERS = {
    'reg', 'set_reg', 'push', 'pop', 'logic', 'sub', 'add', 'shift',
    'sign_extend', 'incdec', 'imul', 'test_condition', 'multiply',
    'double_shift', 'divide', 'scan', 'bit_op', 'bit_address', 'segment_address',
    'GuestFault', 'FaultKind', 'CF', 'PF', 'AF', 'ZF', 'SF', 'OF', 'DF',
    'STATUS_FLAGS', 'EAX', 'ECX', 'EDX', 'EBX', 'ESP', 'EBP', 'ESI', 'EDI',
    'string_op',
    'x87_memory','x87_execute','x87_wait','x87_clear','x87_init','x87_env',
    'sse_execute','sse_memory','sse_i32_to_sd','sse_sd_to_i32','sse_sd_memory_to_i32',
    'xmm_dword','xmm_word','xmm_insert_word','xmm_from_dword','xmm_byte_mask',
    'xmm_shift_qwords','xmm_shuffle_reg','xmm_shuffle_memory','xmm_shuffle_pd_reg','xmm_shuffle_pd_memory',
    'guest_cpuid','guest_rdtsc','guest_popfd',
}
FLAG_BITS={'CF':1,'PF':4,'AF':16,'ZF':64,'SF':128,'OF':2048}
STATUS_FLAGS=sum(FLAG_BITS.values())
ALL_FLAGS=STATUS_FLAGS|(STATUS_FLAGS<<16)
FLAG_WRITES={
    'add':ALL_FLAGS,'sub':ALL_FLAGS,
    'logic':(STATUS_FLAGS^FLAG_BITS['AF'])|(STATUS_FLAGS<<16),
    'incdec':(STATUS_FLAGS^FLAG_BITS['CF'])|((STATUS_FLAGS^FLAG_BITS['CF'])<<16),
    'imul':FLAG_BITS['CF']|FLAG_BITS['OF']|(STATUS_FLAGS<<16),
}
PENDING_TRANSCENDENTAL=re.compile(r'wr_x87_217_r(?:240|241|242|243|249|254|255)\b')
CONTRACT_FAULT='throw wr::GuestFault(wr::FaultKind::unsupported,s.eip,"x87 transcendental result/status contract is not established");'


def call_arguments(text,start):
    depth=1;arguments=[];at=start
    while at<len(text) and depth:
        character=text[at]
        if character=='(':depth+=1
        elif character==')':depth-=1
        if (character==',' and depth==1) or not depth:
            arguments.append(text[start:at].strip());start=at+1
        at+=1
    return arguments if not depth else []


def flag_effect(body,following):
    """Conservative raw-flag AND defined-bit liveness, including fault exits.

    A potentially faulting operation observes the complete incoming state.
    Calls, returns, stops and unresolved transfers observe it too. This keeps
    exact exception snapshots; only pure register arithmetic can lose work.
    """
    plain=body.replace('s.flags=f.flags;s.defined_flags=f.defined_flags;','')
    kinds={edge['kind'] for edge in following}
    helpers=re.findall(r'\bwr::(\w+)\(',plain)
    operations=[name for name in helpers if name in FLAG_WRITES]
    allowed={'reg','set_reg','sign_extend','mask'}|set(FLAG_WRITES)
    boundary=bool(kinds & {'return','stop','call_direct','call_import','jump_import','call_indirect','jump_indirect'})
    unsafe=(boundary or 'm.' in plain or 'throw ' in plain or 'if(' in plain or
            's.defined_flags' in plain or any(name not in allowed for name in helpers))
    # Every rewrite has one unconditional helper with a statically valid
    # width. The source register accesses are literal-index emitter operands.
    operation=operations[0] if len(operations)==1 else None
    if operation and not re.search(r'wr::'+operation+r'\(f,[^;]*,(?:8|16|32)(?:,[^;]*)?\)',plain):unsafe=True
    for reg in re.finditer(r'wr::(?:reg|set_reg)\(s,([^,\)]+)',plain):
        if not reg[1].isdigit() or not 0<=int(reg[1])<8:unsafe=True
    for helper in re.finditer(r'wr::(reg|set_reg|sign_extend|mask)\(',plain):
        args=call_arguments(plain,helper.end());name=helper[1]
        if name in ('reg','set_reg'):
            width_at=2 if name=='reg' else 3
            if len(args)<width_at or len(args)>width_at+2:unsafe=True;continue
            width=args[width_at] if len(args)>width_at else '32'
            shift=args[width_at+1] if len(args)>width_at+1 else '0'
            if width not in ('8','16','32') or shift not in ('0','8') or (shift=='8' and width!='8'):unsafe=True
        else:
            if not args or args[-1] not in ('8','16','32'):unsafe=True
    reads=0
    flag_reads=list(re.finditer(r'\bs\.flags\b',plain))
    if flag_reads:
        # Read exact masks for the emitter's ordinary conditions and ADC/SBB.
        stripped=plain
        for mask in re.finditer(r's\.flags&((?:wr::(?:CF|PF|AF|ZF|SF|OF))(?:\|wr::(?:CF|PF|AF|ZF|SF|OF))*)',plain):
            for name in re.findall(r'wr::(CF|PF|AF|ZF|SF|OF)',mask[1]):reads|=FLAG_BITS[name]
            stripped=stripped.replace(mask[0],'known_flag_operand')
        if 's.flags' in stripped:reads=ALL_FLAGS
    if unsafe:reads=ALL_FLAGS
    return reads,FLAG_WRITES.get(operation,0),operation if operation and not unsafe else None


def dead_flag_operations(record,bodies,edges):
    nodes=set(record['locations']);predecessors=defaultdict(set);successors={};effects={}
    for pc in nodes:
        successors[pc]={edge['to'] for edge in edges[pc]
                        if edge['to'] in nodes and edge['kind'] not in ('call_direct','data_callback_initial')}
        for target in successors[pc]:predecessors[target].add(pc)
        effects[pc]=flag_effect(bodies[pc][0],edges[pc])
    live={pc:0 for pc in nodes};pending=deque(sorted(nodes,reverse=True));queued=set(nodes)
    while pending:
        pc=pending.popleft();queued.remove(pc)
        reads,writes,_=effects[pc]
        out=0
        for target in successors[pc]:out|=live[target]
        value=reads|(out&~writes)
        if value!=live[pc]:
            live[pc]=value
            for source in predecessors[pc]:
                if source not in queued:pending.append(source);queued.add(source)
    result={}
    for pc in nodes:
        _,writes,operation=effects[pc]
        out=0
        for target in successors[pc]:out|=live[target]
        if operation and not out&writes:result[pc]=operation
    return result


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write(name, text):
    path = OUT / name
    if not path.exists() or path.read_text() != text:
        path.write_text(text, encoding='utf-8', newline='\n')


def analyze():
    graph = json.loads((ROOT / 'logs/cfg.json').read_text())
    instructions = {i['address']: i for i in graph['instructions']}
    edges = defaultdict(list)
    for edge in graph['edges']:
        edges[edge['from']].append(edge)
    tables = {t['branch']: t for t in graph['recovered_tables']}
    bodies, provenance = {}, {}
    for page in sorted((ROOT / 'generated/wa2-win64').glob('code_page_*.cpp')):
        provenance[page.relative_to(ROOT).as_posix()] = digest(page)
        for match in CASE.finditer(page.read_text()):
            pc = int(match[1], 16)
            proof = PROOF.match(match[2])
            if proof is None or pc in bodies:
                raise ValueError(f'invalid or duplicate oracle at {pc:#x}')
            raw = bytes(map(int, proof[1].split(',')))
            if raw.hex() != instructions[pc]['bytes']:
                raise ValueError(f'CFG/oracle byte mismatch at {pc:#x}')
            bodies[pc] = (match[2][proof.end():].strip(), raw, match[2])
    if set(bodies) != set(instructions):
        raise ValueError('existing CFG and generated instruction inventory differ')
    records = {}
    native = json.loads((ROOT/'port/native_functions.json').read_text())
    retired = json.loads((ROOT/'port/retired_modules.json').read_text())
    retired_ranges = [(int(m['start'], 0), int(m['end'], 0)) for m in retired['modules']]
    retired_ranges += [(f['start'], f['end']) for f in native['functions']]
    for entry in sorted(set(graph['function_candidates'])):
        seen, calls, reasons, pending = set(), set(), set(), [entry]
        while pending and len(seen) <= 8192:
            pc = pending.pop()
            if pc in seen:
                continue
            if pc not in bodies:
                reasons.add('missing_instruction_target')
                continue
            seen.add(pc)
            if any(start <= pc < end for start, end in retired_ranges):
                reasons.add('already_replaced_native_body')
            body = bodies[pc][0]
            if set(re.findall(r'wr::(\w+)', body)) - HELPERS:
                reasons.add('unported_instruction_semantics')
            if PENDING_TRANSCENDENTAL.search(body):
                # Preserve an explicit boundary until transcendental status
                # liveness and extended-precision behavior are validated.
                reasons.add('transcendental_contract_pending')
            for edge in edges[pc]:
                kind, target = edge['kind'], edge['to']
                if kind == 'call_direct':
                    calls.add(target)
                elif kind == 'return':
                    pass
                elif kind == 'jump_indirect' and pc in tables:
                    recovered = set(tables[pc]['targets'])
                    static = {e['to'] for e in edges[pc] if e['kind'] == 'jump_table_static'}
                    if recovered != static or not recovered or not tables[pc]['runtime_target_still_checked']:
                        reasons.add('inconsistent_jump_table_proof')
                elif kind in ('call_indirect', 'jump_indirect'):
                    # The target is resolved against the immutable compiled
                    # entry inventory after unwinding the native call stack.
                    pass
                elif kind in ('call_import', 'jump_import'):
                    pass
                elif kind == 'stop':
                    if not re.fullmatch(r'throw wr::GuestFault\(wr::FaultKind::breakpoint,s.eip,"guest breakpoint"\);',body):
                        reasons.add('unported_stop_semantics')
                elif kind == 'data_callback_initial':
                    # This is a discovery seed, not an executed control edge.
                    pass
                elif target is not None:
                    pending.append(target)
                else:
                    reasons.add('unknown_edge')
        if pending:
            reasons.add('function_graph_limit')
        records[entry] = dict(locations=sorted(seen), calls=sorted(calls), reasons=sorted(reasons))
    # A rare x87 fallback must not retire the whole CRT math function,
    # including its ordinary, already precise SSE path. Compile the known
    # paths and retain a checked stop at each unestablished instruction.
    admitted = {entry for entry,r in records.items() if set(r['reasons'])<= {'transcendental_contract_pending'}}
    return graph, bodies, edges, records, admitted, provenance


def emit_body(pc, body, edges, owners,positions):
    following = edges[pc]
    kinds = {e['kind'] for e in following}
    if 'return' in kinds:
        return body + '\nreturn;'
    if 'stop' in kinds:
        return body
    if kinds & {'call_import','jump_import','call_indirect'}:
        # Preserve the original target load and stack push, including their
        # fault ordering. Only explicitly certified, nonblocking leaf services
        # may run here; callbacks/waits still unwind outside the memory scope.
        continuations={e['to'] for e in following if e['kind']=='call_continuation'}
        service='call_source_indirect(s,m,scope)' if 'call_indirect' in kinds else 'call_leaf_service(s,m)'
        if len(continuations)==1:
            continuation,=continuations
            if owners.get(continuation)==owners[pc]:
                return (body+f'\nif constexpr(!Metered)if({service}){{\n'
                        f'if(s.eip==0x{continuation:x}u)goto L_{continuation:x};\nreturn;}}\nreturn;')
        if 'jump_import' in kinds:
            return body+f'\nif constexpr(!Metered)(void){service};\nreturn;'
        return body+'\nreturn;'
    if 'jump_indirect' in kinds:
        targets = sorted({e['to'] for e in following if e['kind'] == 'jump_table_static'})
        if not targets:
            return body+'\nif constexpr(!Metered)(void)call_source_indirect(s,m,scope);\nreturn;'
        return body+'\nswitch(s.eip){\n'+''.join(f'case 0x{x:x}u:goto L_{x:x};\n' for x in targets)+(
            'default:throw wr::GuestFault(wr::FaultKind::untranslated,s.eip,"unproven static jump-table target");\n}')
    if 'call_direct' in kinds:
        callee, = {e['to'] for e in following if e['kind'] == 'call_direct'}
        continuation, = {e['to'] for e in following if e['kind'] == 'call_continuation'}
        call = re.fullmatch(r'\{auto target=0x([0-9a-f]+)u;wr::push\(s,m,0x([0-9a-f]+)u\); s.eip=target;\}', body)
        if not call or (int(call[1],16),int(call[2],16))!=(callee,continuation):
            raise ValueError(f'call proof disagrees with oracle at {pc:#x}')
        if callee not in owners:
            # Native subsystem gateways and explicitly deferred functions are
            # host boundaries, never an instruction interpreter fallback.
            return body+'\nreturn;'
        return (body + f'\nf_{owners[callee]:x}<Metered>(s,m,scope,budget,{positions[callee]}u);\n'
                f'if(s.eip!=0x{continuation:x}u)return;\ngoto L_{continuation:x};')
    if kinds == {'jump_direct'}:
        branch = re.fullmatch(r'\{auto target=0x([0-9a-f]+)u;s.eip=target;\}', body)
        expected = {e['to'] for e in following}
        if not branch or {int(branch[1], 16)} != expected:
            raise ValueError(f'unrecognized direct jump at {pc:#x}')
        return f'goto L_{branch[1]};'
    # Only the final PC assignment is rewritten. Data constants are untouched.
    match = re.search(r'\ns\.eip=([^;]+);$', '\n' + body)
    if not match:
        raise ValueError(f'unrecognized continuation at {pc:#x}')
    expression = match[1]
    prefix = ('\n' + body)[:match.start()].strip()
    targets = [int(s, 16) for s in re.findall(r'0x([0-9a-f]+)u', expression)]
    expected = {e['to'] for e in following if e['to'] is not None and e['kind'] != 'data_callback_initial'}
    if set(targets) != expected:
        raise ValueError(f'branch proof disagrees with oracle at {pc:#x}')
    if re.fullmatch(r'0x[0-9a-f]+u', expression):
        tail = f'goto L_{targets[0]:x};'
    else:
        branch = re.fullmatch(r'(.+)\?0x([0-9a-f]+)u:0x([0-9a-f]+)u', expression)
        if not branch:
            raise ValueError(f'unrecognized conditional at {pc:#x}')
        tail = f'if({branch[1]})goto L_{branch[2]};\ngoto L_{branch[3]};'
    return prefix + '\n' + tail


def fixed_memory(body):
    """Specialize only literal-width accesses through the already held scope.

    The original checked Memory accessors remain the differential oracle.
    Scope preserves permission checks, cross-page atomicity and code epochs.
    """
    edits = []
    for match in re.finditer(r'\bm\.(load|store)\(', body):
        if edits and match.start() < edits[-1][1]:
            continue
        depth, start, arguments, at = 1, match.end(), [], match.end()
        while at < len(body) and depth:
            char = body[at]
            if char == '(':
                depth += 1
            elif char == ')':
                depth -= 1
            if (char == ',' and depth == 1) or depth == 0:
                arguments.append(body[start:at].strip())
                start = at + 1
            at += 1
        expected = 2 if match[1] == 'load' else 3
        if depth or len(arguments) != expected or arguments[-1] not in ('8','16','32'):
            raise ValueError('memory shape is not statically proven: '+body[match.start():at])
        replacement = f'scope.{match[1]}<{arguments[-1]}>('+','.join(fixed_memory(arg) for arg in arguments[:-1])+')'
        edits.append((match.start(), at, replacement))
    for start, end, replacement in reversed(edits):
        body = body[:start] + replacement + body[end:]
    return body


def portable_floating(body):
    body=re.sub(r'\bwr_x87_(\d+)_(m|r)(\d+)\b',
                lambda m:f'wa2::aot::fp::x87<{m[1]},{m[3]},{str(m[2]=="m").lower()}>',body)
    body=re.sub(r'\bwr_sse_to_i32_(\d+)_(m|r)(\d*)\b',
                lambda m:f'wa2::aot::fp::sse_to_i32<{m[1]},{m[3] or "0"},{str(m[2]=="m").lower()}>',body)
    body=re.sub(r'\bwr_sse_(\d+)_(\d+)_(m|r)(\d+)(?:_i(\d+))?\b',
                  lambda m:f'wa2::aot::fp::sse<{m[1]},{m[2]},{m[4]},{str(m[3]=="m").lower()},{m[5] or "0"}>',body)
    return re.sub(r'wr::(x87_execute|x87_memory|sse_execute|sse_memory|sse_i32_to_sd|sse_sd_to_i32|sse_sd_memory_to_i32)\(s,(m,)?(wa2::aot::fp::(?:x87|sse|sse_to_i32)<[^>]+>),',
                  lambda m:f'wa2::aot::fp::{m[1]}<{m[3]}>(s,'+(m[2] or ''),body)


def merge_bodies(records, admitted):
    """A shared epilogue has one owner, including alternate callback entries."""
    parent = {entry:entry for entry in admitted}
    def root(entry):
        while entry != parent[entry]:
            parent[entry] = parent[parent[entry]]
            entry = parent[entry]
        return entry
    locations = {}
    for entry in sorted(admitted):
        for pc in records[entry]['locations']:
            if pc in locations:
                a,b=root(entry),root(locations[pc])
                parent[max(a,b)]=min(a,b)
            else:
                locations[pc]=entry
    members=defaultdict(list)
    for entry in sorted(admitted):
        members[root(entry)].append(entry)
    merged={}
    for owner,entries in sorted(members.items()):
        merged[owner]=dict(entries=entries,
            locations=sorted({pc for entry in entries for pc in records[entry]['locations']}),
            calls=sorted({pc for entry in entries for pc in records[entry]['calls']}))
    owners={pc:root(entry) for pc,entry in locations.items()}
    for record in merged.values():
        record['bound_callees']=sorted({owners[pc] for pc in record['calls'] if pc in owners})
        record['boundary_callees']=sorted(set(record['calls'])-owners.keys())
    return merged,owners


def floating_neutral_leaves(merged,bodies,edges,injected_owners):
    """Prove small closed integer-only leaves cannot observe local FP state.

    No source/host calls, unknown transfers, native replacements or FP/SIMD
    helpers are admitted. Memory faults unwind the caller's FP frame before
    their state is observed. Integer/register barriers remain unchanged.
    """
    integer={'reg','set_reg','push','pop','mask','logic','sub','add','shift',
        'sign_extend','incdec','imul','test_condition','multiply','double_shift',
        'divide','scan','bit_op','bit_address','segment_address','string_op'}
    internal={'fallthrough','branch_taken','branch_not_taken','jump_direct'}
    result=set()
    for owner,record in merged.items():
        if owner in injected_owners or record['calls'] or len(record['locations'])>128:continue
        locations=set(record['locations']);valid=True;returns=0
        for pc in locations:
            body=bodies[pc][0]
            if ('.fp' in body or re.search(r'\bwr_(?:x87|sse)_',body) or
                set(re.findall(r'\bwr::(\w+)\(',body))-integer):valid=False;break
            for edge in edges[pc]:
                if edge['kind']=='return':returns+=1
                elif edge['kind'] not in internal or edge['to'] not in locations:valid=False;break
            if not valid:break
        if valid and returns:result.add(owner)
    return result


def register_floating(body,neutral_callees=frozenset()):
    """Keep the render function's x87 values local until an ABI boundary.

    Kernel encodings remain C++ template constants. This does not decode or
    dispatch an instruction at runtime. Checked/metered bodies use the original
    serialization, and all state consumers/callees must be explicit here.
    """
    body=re.sub(r'wa2::aot::fp::x87_memory<wa2::aot::fp::x87<([^>]+)>>\(s,m,',r'floating.template memory<\1>(',body)
    body=re.sub(r'wa2::aot::fp::x87_execute<wa2::aot::fp::x87<([^>]+)>>\(s,',r'floating.template execute<\1>(',body)
    body=body.replace('s.fp.get16(0)','floating.word(0)').replace('s.fp.get16(2)','floating.word(2)')
    if 's.fp' in body or 'wr::x87_' in body or 'wa2::aot::fp::x87_' in body:
        raise ValueError('unbridged floating state access in render register function: '+body)
    def call_barrier(match):
        if int(match[2],16) in neutral_callees:return match[1]
        return 'floating.external([&]{'+match[1]+'});'
    body=re.sub(r'^((?:dispatch_)?f_([0-9a-f]+)<Metered>\([^;]+;)$',call_barrier,body,flags=re.M)
    body=body.replace('call_leaf_service(s,m)','floating.external([&]{return call_leaf_service(s,m);})')
    body=body.replace('call_source_indirect(s,m,scope)','floating.external([&]{return call_source_indirect(s,m,scope);})')
    return body


def register_integers(body):
    """Lift literal register accesses into local C++ values, with ABI barriers.

    Register indexes/widths are proven here. No address or opcode is decoded
    at runtime. An external consumer first commits the complete source state.
    """
    edits=[]
    for match in re.finditer(r'wr::(reg|set_reg|push|pop)\(',body):
        if edits and match.start()<edits[-1][1]:continue
        arguments=call_arguments(body,match.end());depth=1;end=match.end()
        while depth:
            if body[end]=='(':depth+=1
            elif body[end]==')':depth-=1
            end+=1
        if not arguments or arguments[0]!='s':continue
        name=match[1]
        if name in ('reg','set_reg'):
            value_at=2 if name=='set_reg' else None;width_at=3 if name=='set_reg' else 2
            if len(arguments)<width_at or len(arguments)>width_at+2:raise ValueError('unproven native register shape')
            index=arguments[1];width=arguments[width_at] if len(arguments)>width_at else '32'
            shift=arguments[width_at+1] if len(arguments)>width_at+1 else '0'
            if not index.isdigit() or not 0<=int(index)<8 or width not in ('8','16','32') or shift not in ('0','8') or (shift=='8' and width!='8'):
                raise ValueError('unproven native register operand')
            replacement=f'integer.template {"set" if value_at else "get"}<{index},{width},{shift}>('
            replacement+=(register_integers(arguments[value_at]) if value_at else '')+')'
        else:
            expected=3 if name=='push' else 2
            if len(arguments) not in (expected,expected+1) or arguments[1]!='m':raise ValueError('unproven native stack shape')
            width=arguments[expected] if len(arguments)>expected else '32'
            if width not in ('16','32'):raise ValueError('unproven native stack width')
            replacement=f'integer.template {name}<{width}>('+(register_integers(arguments[2]) if name=='push' else '')+')'
        edits.append((match.start(),end,replacement))
    for start,end,replacement in reversed(edits):body=body[:start]+replacement+body[end:]
    body=re.sub(r'\bs\.r\[(wr::(?:EAX|ECX|EDX|EBX|ESP|EBP|ESI|EDI)|[0-7])\]',r'integer.template word<\1>()',body)
    # Calls below can read/write the actual Cpu integer state. Exact fault and
    # callback prefixes must not see stale private values. Floating kernels and
    # condition tests use only FP/flags; all other consumers are rejected below.
    external=r'(?:(?:dispatch_)?f_[0-9a-f]+<Metered>|call_leaf_service|call_source_indirect|wr::(?:divide|multiply|guest_rdtsc|aot_string_op<Metered>|wa2_sort_objects_full|wa2_vertex_full))'
    edits=[]
    for match in re.finditer(external+r'\(s(?:,|\))',body):
        start=match.start();opening=body.index('(',start);depth=1;end=opening+1
        while depth:
            if body[end]=='(':depth+=1
            elif body[end]==')':depth-=1
            end+=1
        edits.append((start,end,'integer.external([&]{return '+body[start:end]+';})'))
    for start,end,replacement in reversed(edits):body=body[:start]+replacement+body[end:]
    if re.search(r'wr::(?:reg|set_reg|push|pop)\(s,|\bs\.r\b',body):raise ValueError('unbridged native integer register access: '+body)
    consumers=set(re.findall(r'\b([\w:]+(?:<[^\n;()]+>)?)\(s(?:,|\))',body))
    for consumer in consumers:
        if re.fullmatch(external,consumer) or consumer=='wr::test_condition' or consumer.startswith('wa2::aot::fp::'):continue
        raise ValueError('unreviewed native integer state consumer: '+consumer)
    return body


def vertex_grid(bodies,edges,owners,positions,instructions,begin=0x43cc17,end=0x43d5d7,filename='vertex_grid.inc',entry_depth=0,exit_depth=0):
    """Compile the closed, memory-only grid region into one local transaction.

    Every address is still checked against the admitted frame, and rejecting an
    address rolls back the whole region. No external call is allowed here.
    """
    pending=[begin];seen=set()
    while pending:
        pc=pending.pop()
        if pc==end or pc in seen:continue
        if pc not in bodies:raise ValueError('missing geometry location')
        seen.add(pc);pending += [edge['to'] for edge in edges[pc] if edge['to'] is not None]
    nodes=sorted(seen)
    expected={'fallthrough','branch_taken','branch_not_taken','jump_direct'}
    successors={pc:{edge['to'] for edge in edges[pc]} for pc in nodes}
    # Prove one stack depth at every node, including every back edge/join.
    # The scalar path admits only this region's empty-stack clipping entry.
    depths={begin:entry_depth};pending=deque([begin])
    while pending:
        pc=pending.popleft()
        if pc==end:continue
        mnemonic=instructions[pc]['mnemonic'];depth=depths[pc]
        if mnemonic in ('fld','fild','fld1','fldz'):delta=1
        elif mnemonic in ('fstp','fistp','fcomp','faddp','fsubp','fsubrp','fmulp','fdivp','fdivrp'):delta=-1
        elif mnemonic=='fcompp':delta=-2
        else:delta=0
        after=depth+delta
        if not 0<=after<=8:raise ValueError('unbalanced static geometry stack')
        for target in successors[pc]:
            if target in depths:
                if depths[target]!=after:raise ValueError('conflicting geometry stack join')
            else:depths[target]=after;pending.append(target)
    if set(depths)!={*nodes,end}:raise ValueError('geometry region is not closed')
    grid=filename=='vertex_grid.inc'
    if (grid and (depths[0x43ce95]!=3 or depths[end]!=3)) or (not grid and depths[end]!=exit_depth):
        raise ValueError(f'geometry stack entry/exit contract changed: {filename}: {depths[end]}')
    effects={}
    for pc in nodes:
        body=emit_body(pc,bodies[pc][0],edges,owners,positions)
        plain=body.replace('s.flags=f.flags;s.defined_flags=f.defined_flags;','')
        operations=re.findall(r'wr::(add|sub|logic|incdec|imul)\(f,',plain)
        operation=operations[0] if len(operations)==1 else None
        reads=0
        for name in re.findall(r's\.flags&wr::(CF|PF|AF|ZF|SF|OF)',plain):reads|=FLAG_BITS[name]
        if re.sub(r's\.flags&wr::(?:CF|PF|AF|ZF|SF|OF)','known_flag',plain).find('s.flags')>=0:
            reads=ALL_FLAGS
        effects[pc]=(reads,FLAG_WRITES.get(operation,0),operation)
    # Admission/transaction rollback makes intermediate fault snapshots
    # unobservable. Branches and the committed exit still see exact flags.
    live={pc:0 for pc in nodes};live[end]=ALL_FLAGS
    changed=True
    while changed:
        changed=False
        for pc in reversed(nodes):
            out=0
            for target in successors[pc]:out|=live.get(target,ALL_FLAGS)
            reads,writes,_=effects[pc];value=reads|(out&~writes)
            if live[pc]!=value:live[pc]=value;changed=True
    output=['// Generated from cached checked C++ and CFG; same for all hosts.']
    if grid:output += ['if(s.eip==0x43ce95u)goto L_43ce95;']
    output += [f'goto L_{begin:x};']
    for pc in nodes:
        for edge in edges[pc]:
            if edge['kind'] not in expected or edge['to'] not in {*nodes,end}:
                raise ValueError(f'vertex grid is not a closed pure region: {pc:#x}: {edge}')
        body=register_floating(fixed_memory(portable_floating(emit_body(pc,bodies[pc][0],edges,owners,positions))))
        out=0
        for target in successors[pc]:out|=live.get(target,ALL_FLAGS)
        _,writes,operation=effects[pc]
        if operation and not out&writes:body=body.replace('wr::'+operation+'(',f'wa2::aot::value::{operation}<false>(')
        if 'm.' in body or 'wr::push' in body or 'wr::pop' in body or 'external(' in body:
            raise ValueError(f'unbridged vertex grid access: {pc:#x}')
        body=body.replace('floating.template memory<',f'floating.template memory_at<{depths[pc]},').replace('floating.template execute<',f'floating.template execute_at<{depths[pc]},')
        body=body.replace('floating.word(',f'floating.template word_at<{depths[pc]}>(')
        body=body.replace('scope.load<','scope.template load<').replace('scope.store<','scope.template store<')
        # Only arithmetic operations record FIP. Other instructions have no
        # externally observable intermediate PC in this admitted transaction.
        output += [f'L_{pc:x}: {{',*([f's.eip=0x{pc:x}u;'] if 'floating.template' in body else []),body,'}']
    output += [f'L_{end:x}: s.eip=0x{end:x}u;',f'floating.template finish_at<{depths[end]}>();']
    write(filename,'\n'.join(output)+'\n')


def main():
    if os.name=='nt':
        import ctypes
        kernel=ctypes.WinDLL('kernel32',use_last_error=True);kernel.GetCurrentProcess.restype=ctypes.c_void_p
        kernel.SetPriorityClass.argtypes=[ctypes.c_void_p,ctypes.c_uint]
        if not kernel.SetPriorityClass(kernel.GetCurrentProcess(),0x4000):raise ctypes.WinError(ctypes.get_last_error())
    contract = json.loads((ROOT/'config/porting_contract.json').read_text())['rules']
    for rule in ('one_game_implementation_for_all_platforms','one_aot_source_set_for_all_host_architectures',
                 'runtime_instruction_decoder_forbidden','jit_forbidden'):
        if contract[rule] is not True:
            raise ValueError('shared AOT contract is not enabled: '+rule)
    OUT.mkdir(parents=True, exist_ok=True)
    graph, bodies, edges, records, admitted, provenance = analyze()
    pending_semantics={pc for pc,(body,_,_) in bodies.items() if PENDING_TRANSCENDENTAL.search(body)}
    function_ports=json.loads((ROOT/'config/native_math_contracts.json').read_text())['functions']
    function_ports+=json.loads((ROOT/'config/native_render_contracts.json').read_text())['functions']
    ports_by_entry={int(port['entry'],0):port for port in function_ports}
    if len(ports_by_entry)!=len(function_ports):raise ValueError('duplicate native function port')
    merged,owners=merge_bodies(records,admitted)
    subtitle_contract=json.loads((ROOT/'config/subtitle_hooks.json').read_text())
    subtitle_events={int(event['entry'],0):event for event in subtitle_contract['events']}
    if set(subtitle_events)!={0x40f400,0x40eda0,0x406270,0x40f590,0x40ec30}:
        raise ValueError('Unexpected main-story subtitle event entry')
    for entry,event in subtitle_events.items():
        if entry not in merged or [a for a in admitted if owners[a]==entry]!=[entry]:
            raise ValueError('Subtitle event must own exactly one cached source function')
        locations=merged[entry]['locations']
        snapshot=b''.join(pc.to_bytes(4,'little')+bodies[pc][1] for pc in locations)
        returns=[pc for pc in locations if any(e['kind']=='return' for e in edges[pc])]
        if hashlib.sha256(snapshot).hexdigest()!=event['cached_body_sha256'] or returns!=event['return_locations']:
            raise ValueError('Cached subtitle audio ABI changed: '+event['event'])
    positions={pc:index for record in merged.values() for index,pc in enumerate(record['locations'])}
    vertex_grid(bodies,edges,owners,positions,{row['address']:row for row in graph['instructions']})
    vertex_grid(bodies,edges,owners,positions,{row['address']:row for row in graph['instructions']},
                0x43bba8,0x43bde0,'sprite_geometry.inc')
    vertex_grid(bodies,edges,owners,positions,{row['address']:row for row in graph['instructions']},
                0x4089c5,0x408a37,'snow_motion.inc',3)
    from generate_weather_batch import generate as generate_weather_batch
    weather_own,weather_external=generate_weather_batch(bodies,edges,owners,positions,graph['instructions'])
    for entry,port in ports_by_entry.items():
        if entry not in owners:raise ValueError('native function port entry must remain a checked static location')
        expected=bytes.fromhex(port['signature_hex']);actual=bytearray();pc=entry
        while len(actual)<len(expected):
            if pc not in bodies:raise ValueError('native function signature crosses a missing cached instruction')
            raw=bodies[pc][1];actual.extend(raw);pc+=len(raw)
        if actual!=expected:raise ValueError('native function port signature mismatch: '+port['name'])
    loops=json.loads((ROOT/'config/native_loops.json').read_text())['loops']
    loops_by_entry={loop['entry']:loop for loop in loops}
    if len(loops_by_entry)!=len(loops):raise ValueError('duplicate native loop entry')
    for loop in loops:
        if loop['entry'] not in owners or any(owners.get(pc)!=owners[loop['entry']] for pc in loop['exit_targets']):
            raise ValueError('native loop must resume inside the same compiled function: '+loop['name'])
        if loop['implementation']=='recompiled/native_weather.cpp':
            if loop.get('code_verification')!='shared_aot_direct_closure':
                raise ValueError('Weather batches require the immutable direct-call closure proof')
            if loop['exit_targets']!=weather_own or not set(weather_external).issubset(loop.get('external_exit_targets',[])):
                raise ValueError('Weather prefix exit proof changed; update the explicit native loop contract')
            if any(pc not in owners for pc in loop.get('external_exit_targets',[])):
                raise ValueError('Weather prefix return must be a checked static source location')
        if 'signature_hex' in loop:
            actual=bytearray();pc=loop.get('signature_entry',loop['entry'])
            if owners.get(pc)!=owners[loop['entry']]:
                raise ValueError('native loop signature must remain in its compiled function: '+loop['name'])
            if pc!=loop['entry'] and bodies[loop['entry']][1].hex()!=loop.get('preheader_signature_hex'):
                raise ValueError('native loop preheader signature changed: '+loop['name'])
            while pc<loop['end']:
                if pc not in bodies:raise ValueError('native loop crosses an undiscovered instruction: '+loop['name'])
                raw=bodies[pc][1];actual.extend(raw);pc+=len(raw)
            if pc!=loop['end'] or actual.hex()!=loop['signature_hex']:
                raise ValueError('native loop cached-source signature changed: '+loop['name'])
    fp_neutral=floating_neutral_leaves(merged,bodies,edges,
        {owners[pc] for pc in ports_by_entry}|{owners[loop['entry']] for loop in loops})
    dead_flags={pc:operation for record in merged.values() for pc,operation in dead_flag_operations(record,bodies,edges).items()}
    ordered = sorted(merged)
    hot_config=json.loads((ROOT/'config/aot_hot_functions.json').read_text())
    if (hot_config['shared_optimization'],hot_config['other_generated_functions'],hot_config['floating_arithmetic_optimization'])!=('O2','Oz','O2'):
        raise ValueError('hot function compiler policy must match the common CMake targets')
    hot=set(hot_config['entries'])
    if len(hot)!=len(hot_config['entries']) or not hot.issubset(merged):
        raise ValueError('hot functions must name distinct existing merged static bodies')
    register_entries=set(hot_config['register_floating_point_entries'])
    if not register_entries.issubset(hot):
        raise ValueError('register floating-point entries must be selected measured hot bodies')
    integer_entries=set(hot_config['register_integer_entries'])
    if integer_entries!=hot:raise ValueError('native integer register lifting must cover exactly the measured hot bodies')
    if set(subtitle_events)&integer_entries:
        raise ValueError('Subtitle audio observers require their source ABI registers to be committed at returns')
    header = ['// Generated from the existing C++ oracle; common to all hosts.',
              '#pragma once', '#include "functions.hpp"', 'namespace wa2::aot {']
    for entry in ordered:
        parameters='bool Metered,bool NativeRegisters=true' if entry in integer_entries else 'bool Metered'
        header.append(f'template<{parameters}> void f_{entry:x}(Context&,wr::Memory&,Scope&,std::uint64_t&,std::uint32_t);')
    for entry in sorted(integer_entries):
        header += [f'template<bool Metered> [[gnu::always_inline]] void dispatch_f_{entry:x}(Context& s,wr::Memory& m,Scope& scope,std::uint64_t& b,std::uint32_t resume){{',
                   f'if constexpr(Metered){{f_{entry:x}<true,true>(s,m,scope,b,resume);return;}}',
                   '#if WA2_AOT_DIAGNOSTICS',
                   f'if(!native_integer_registers_enabled){{f_{entry:x}<false,false>(s,m,scope,b,resume);return;}}',
                   '#endif',f'f_{entry:x}<false,true>(s,m,scope,b,resume);','}']
    header.append('}')
    write('bodies.hpp', '\n'.join(header) + '\n')
    sources, code_spans, all_locations = [], [], set()
    # Fixed shard count keeps build inputs deterministic as functions are added.
    shards=[(f'functions_{shard}.cpp',[entry for entry in ordered[shard::8] if entry not in hot]) for shard in range(8)]
    shards.append(('functions_hot.cpp',sorted(hot)))
    for name,entries in shards:
        sources.append(name)
        output = ['#include "bodies.hpp"','#include "../../port/sprite_sort.hpp"',
                  '#include "../../port/vertex_batch.hpp"','#include "native_string.hpp"', 'namespace wa2::aot {']
        if set(entries)&set(subtitle_events):output.insert(3,'#include "subtitle_events.hpp"')
        includes=sorted({port['header'] for pc,port in ports_by_entry.items() if owners[pc] in entries} |
                        {loop['header'] for loop in loops if owners[loop['entry']] in entries and 'header' in loop})
        output[3:3]=['#include "../../'+name+'"' for name in includes]
        if any(owners[loop['entry']] in entries and loop['implementation'] in ('recompiled/uv_clip.cpp','recompiled/vertex_grid.cpp') for loop in loops):
            output.insert(3,'#include "uv_clip.hpp"')
        if register_entries.intersection(entries):output.insert(3,'#include "fp_registers.hpp"')
        if integer_entries.intersection(entries):output.insert(3,'#include "gpr_registers.hpp"')
        for entry in entries:
            record = merged[entry]
            parameters='bool Metered,bool NativeRegisters' if entry in integer_entries else 'bool Metered'
            output += [f'template<{parameters}> void f_{entry:x}(Context& s,wr::Memory& m,Scope& scope,std::uint64_t& budget,std::uint32_t resume){{']
            if entry in register_entries:output += ['wa2::aot::fp::RegisterFrame<Metered> floating(s,m,scope);']
            if entry in integer_entries:output += ['wa2::aot::IntegerRegisters<Metered || !NativeRegisters> integer(s,m,scope);']
            output += [f'if(resume=={positions[entry]}u)goto L_{entry:x};','switch(resume){']
            output += [f'case {positions[pc]}u:goto L_{pc:x};' for pc in record['locations']]
            output += ['default:throw wr::GuestFault(wr::FaultKind::untranslated,s.eip,"invalid native function resume entry");','}']
            spans = []
            for pc in record['locations']:
                all_locations.add(pc)
                body, raw, _ = bodies[pc]
                native_body = CONTRACT_FAULT if pc in pending_semantics else emit_body(pc, body, edges, owners,positions).replace('wr::string_op(', 'wr::aot_string_op<Metered>(')
                if entry in subtitle_events and pc in subtitle_events[entry]['return_locations']:
                    if not native_body.endswith('\nreturn;'):
                        raise ValueError('Subtitle event must follow an ordinary source RET')
                    native_body=('const auto subtitle_arguments=std::uint32_t(s.r[wr::ESP]+4u);\n'+
                        native_body[:-len('\nreturn;')]+
                        f'\nnotify_subtitle_return<Metered>(0x{entry:x}u,s,m,subtitle_arguments);\nreturn;')
                for target in integer_entries:native_body=native_body.replace(f'f_{target:x}<Metered>(',f'dispatch_f_{target:x}<Metered>(')
                if pc in dead_flags:
                    operation=dead_flags[pc]
                    native_body=native_body.replace('wr::'+operation+'(',f'wa2::aot::value::{operation}<Metered>(')
                output += [f'L_{pc:x}: {{', f's.eip=0x{pc:x}u;', 'if constexpr(Metered)wr::tick(budget,s.eip);']
                if pc in ports_by_entry:
                    invocation=ports_by_entry[pc]['function']+'(s,m)'
                    if entry in integer_entries:invocation='integer.external([&]{return '+invocation+';})'
                    if entry in register_entries:invocation='floating.external([&]{return '+invocation+';})'
                    output += [f'if constexpr(!Metered)if({invocation})return;']
                if pc in loops_by_entry:
                    loop=loops_by_entry[pc]
                    invocation=loop['function']+'(s,m)'
                    if entry in integer_entries:invocation='integer.external([&]{return '+invocation+';})'
                    if entry in register_entries:invocation='floating.external([&]{return '+invocation+';})'
                    if loop['implementation']=='recompiled/native_weather.cpp':
                        invocation='wa2::aot::native_weather_population(m) && '+invocation
                    output += [f'if constexpr(!Metered)if({invocation}){{switch(s.eip){{',
                               *(f'case 0x{target:x}u:goto L_{target:x};' for target in loop['exit_targets']),
                               *(f'case 0x{target:x}u:return;' for target in loop.get('external_exit_targets',[])),
                               'default:throw wr::GuestFault(wr::FaultKind::unsupported,s.eip,"invalid native loop continuation");}}']
                native_body=fixed_memory(portable_floating(native_body))
                if entry in register_entries:native_body=register_floating(native_body,fp_neutral)
                if entry in integer_entries:native_body=register_integers(native_body)
                output += [native_body, '}']
                if spans and spans[-1][0] + len(spans[-1][1]) == pc:
                    spans[-1][1].extend(raw)
                else:
                    spans.append((pc, bytearray(raw)))
            if entry in integer_entries:
                output += ['}', '#if WA2_AOT_DIAGNOSTICS',
                           f'template void f_{entry:x}<true,true>(Context&,wr::Memory&,Scope&,std::uint64_t&,std::uint32_t);',
                           f'template void f_{entry:x}<false,false>(Context&,wr::Memory&,Scope&,std::uint64_t&,std::uint32_t);','#endif',
                           f'template void f_{entry:x}<false,true>(Context&,wr::Memory&,Scope&,std::uint64_t&,std::uint32_t);']
            else:
                output += ['}', '#if WA2_AOT_DIAGNOSTICS',
                           f'template void f_{entry:x}<true>(Context&,wr::Memory&,Scope&,std::uint64_t&,std::uint32_t);', '#endif',
                           f'template void f_{entry:x}<false>(Context&,wr::Memory&,Scope&,std::uint64_t&,std::uint32_t);']
            code_spans.append((entry, spans))
        output.append('}')
        write(name, '\n'.join(output) + '\n')
    output = ['#include "bodies.hpp"','#include "../static-data/static_data.hpp"', 'namespace wa2::aot {', 'namespace {']
    for entry, spans in sorted(code_spans):
        output.append(f'const CodeSpan code_{entry:x}[]={{'+','.join(
            f'{{0x{pc:x}u,{{static_code_bytes+(0x{pc:x}u-static_code_base),{len(raw)}u}}}}' for pc,raw in spans)+'};')
        if merged[entry]['bound_callees']:
            output.append(f'constexpr std::uint32_t callees_{entry:x}[]={{'+','.join(f'0x{pc:x}u' for pc in merged[entry]['bound_callees'])+'};')
        invocation=f'dispatch_f_{entry:x}' if entry in integer_entries else f'f_{entry:x}'
        output += [f'void entry_{entry:x}(Context& s,wr::Memory& m,Scope& scope,std::uint64_t& b,bool metered,std::uint32_t resume){{',
                   '#if WA2_AOT_DIAGNOSTICS',
                   f'if(metered){invocation}<true>(s,m,scope,b,resume);else {invocation}<false>(s,m,scope,b,resume);',
                   '#else', f'(void)metered;{invocation}<false>(s,m,scope,b,resume);', '#endif', '}']
    output.append('const Function table[]={')
    output += [f'{{0x{entry:x}u,entry_{entry:x},code_{entry:x},'+
               (f'callees_{entry:x}' if merged[entry]['bound_callees'] else '{}')+'},' for entry in ordered]
    output += ['};','struct EntryRange {std::uint32_t first,end,index,ordinal;const std::uint64_t* starts;const std::uint16_t* ranks;};']
    # A range lookup plus an instruction-start bitmap is much smaller than a
    # host pointer for every instruction. Interior instruction bytes fail.
    ranges=[]
    for index,(entry,spans) in enumerate(sorted(code_spans)):
        pcs=merged[entry]['locations']
        for n,(first,raw) in enumerate(spans):
            bitmap=[0]*((len(raw)+63)//64)
            for pc in pcs:
                if first<=pc<first+len(raw):
                    offset=pc-first;bitmap[offset//64]|=1<<(offset%64)
            name=f'starts_{entry:x}_{n}'
            ranks=[];rank=0
            for word in bitmap:ranks.append(rank);rank+=word.bit_count()
            if rank>65535:raise ValueError('resume span rank exceeds compact checkpoint format')
            output.append(f'constexpr std::uint64_t {name}[]={{'+','.join(hex(word)+'ull' for word in bitmap)+'};')
            output.append(f'constexpr std::uint16_t ranks_{entry:x}_{n}[]={{'+','.join(map(str,ranks))+'};')
            ranges.append((first,first+len(raw),index,positions[first],name,f'ranks_{entry:x}_{n}'))
    output.append('const EntryRange entry_ranges[]={')
    ranges.sort()
    output += [f'{{0x{first:x}u,0x{end:x}u,{index}u,{ordinal}u,{name},{ranks}}},' for first,end,index,ordinal,name,ranks in ranges]
    # A tiny page directory narrows an indirect resume to only the ranges
    # touching its 4 KiB page. Instruction-start bits still reject every
    # interior byte; no per-instruction host pointer/address map is added.
    first_page=ranges[0][0]//4096;last_page=(max(r[1] for r in ranges)-1)//4096
    output += ['};','struct RangePage {std::uint16_t first,count;};','constexpr RangePage range_pages[]={']
    for page in range(first_page,last_page+1):
        touching=[i for i,r in enumerate(ranges) if r[0]<(page+1)*4096 and r[1]>page*4096]
        if touching:
            first=touching[0];count=touching[-1]-first+1
            if touching!=list(range(first,first+count)) or first>65535 or count>65535:raise ValueError('noncompact resume page')
        else:first=count=0
        output.append(f'{{{first},{count}}},')
    output += ['};', '}', 'std::span<const Function> functions() noexcept{return table;}',
               'Location locate(std::uint32_t pc) noexcept{',
               f'if(pc<0x{first_page*4096:x}u || pc>=0x{(last_page+1)*4096:x}u)return {{}};',
               f'const auto page=range_pages[(pc-0x{first_page*4096:x}u)/4096];if(!page.count)return {{}};',
               'const auto* first=entry_ranges+page.first;const auto* it=first;',
               'if(page.count>1){it=std::upper_bound(first,first+page.count,pc,[](auto value,const auto& range){return value<range.first;});if(it==first)return {};--it;}',
               'if(pc<it->first || pc>=it->end)return {};',
               'const auto offset=pc-it->first,word=offset/64,bit=offset%64;const auto bits=it->starts[word];',
               'if(!(bits&(std::uint64_t(1)<<bit)))return {};',
               'const auto ordinal=it->ordinal+it->ranks[word]+std::popcount(bits&((std::uint64_t(1)<<bit)-1));',
               'return {&table[it->index],ordinal};','}',
               'const Function* find(std::uint32_t pc) noexcept{return locate(pc).function;}', '}']
    write('table.cpp', '\n'.join(output) + '\n')
    sources.append('table.cpp')
    oracle_sources=['oracle.cpp']
    oracle = ['// Test-only original instruction bodies; never linked into the game.',
              '#include <winrecomp/compiled.hpp>']
    for shard in range(8):
        name=f'oracle_{shard}.cpp';oracle_sources.append(name)
        pcs=sorted(pc for pc in all_locations if (pc>>12)%8==shard)
        part=['#include <winrecomp/compiled.hpp>','#include <winrecomp/sse.hpp>']
        helper_names=sorted({name for pc in pcs for name in re.findall(r'\bwr_(?:x87|sse)_[A-Za-z0-9_]+',bodies[pc][2])})
        part += [f'extern "C" void {helper}(void*,void*,std::uint32_t*);' for helper in helper_names]
        signature=f'bool shared_aot_oracle_{shard}(wr::Cpu& s,wr::Memory& m,std::uint64_t& budget)'
        oracle.append(signature+';')
        part += [signature+'{','(void)budget;switch(s.eip){']
        for pc in pcs:
            if pc in pending_semantics:
                # The instruction oracle remains independent for every
                # admitted operation. At a declared unsupported semantic it
                # checks the precise prefix state and stops before executing
                # that operation; no result/flag equivalence is claimed here.
                part += [f'case 0x{pc:x}u: {{',CONTRACT_FAULT,'}']
            else:part += [f'case 0x{pc:x}u: {{',bodies[pc][2],'return true;}']
        part += ['default:return false;','}}']
        write(name,'\n'.join(part)+'\n')
    oracle += ['bool shared_aot_oracle(wr::Cpu& s,wr::Memory& m,std::uint64_t& budget){',
              'switch((s.eip>>12)%8){']
    oracle += [f'case {shard}:return shared_aot_oracle_{shard}(s,m,budget);' for shard in range(8)]
    oracle += ['default:return false;', '}}']
    oracle += ['bool shared_aot_oracle_known(std::uint32_t pc){','static constexpr std::uint32_t known[]={']
    pcs=sorted(all_locations)
    oracle += [','.join(f'0x{pc:x}u' for pc in pcs[at:at+24])+',' for at in range(0,len(pcs),24)]
    oracle += ['};return std::binary_search(std::begin(known),std::end(known),pc); }']
    used_tables = [t for t in graph['recovered_tables'] if t['branch'] in all_locations]
    oracle += ['void shared_aot_initialize_tables(wr::Memory& memory){']
    for table in used_tables:
        raw = b''.join(target.to_bytes(4,'little') for target in table['targets'])
        oracle += ['{constexpr std::uint8_t data[]={'+','.join(map(str,raw))+'};',
                   f'memory.initialize(0x{table["table"]:x}u,data);}}']
    oracle.append('}')
    write('oracle.cpp', '\n'.join(oracle) + '\n')
    write('sources.cmake', 'set(WA2_SHARED_AOT_SOURCES\n'+''.join(
        f'    "${{CMAKE_CURRENT_LIST_DIR}}/{name}"\n' for name in sources)+')\n'+
        'set(WA2_AOT_ORACLE_SOURCES\n'+''.join(f'    "${{CMAKE_CURRENT_LIST_DIR}}/{name}"\n' for name in oracle_sources)+')\n')
    unresolved=sorted(pending_semantics & all_locations)
    write('semantic_boundaries.hpp', '// Explicit unestablished operations; not verified numerical results.\n'
          '#pragma once\n#include <cstdint>\nnamespace wa2::aot {\n'
          'inline constexpr std::uint32_t pending_transcendental_sites[]={'+
          ','.join(f'0x{pc:x}u' for pc in unresolved)+'};\n}\n')
    deferred = []
    for entry, record in records.items():
        if entry not in admitted:
            deferred.append(dict(entry=entry, reasons=record['reasons'] or ['callee_not_yet_portable'],
                                 unavailable_callees=sorted(set(record['calls']) - admitted)))
    inputs = ['logs/cfg.json', 'port/native_functions.json', 'port/retired_modules.json',
              'config/porting_contract.json', 'config/native_loops.json', 'config/native_math_contracts.json','config/native_render_contracts.json','config/aot_hot_functions.json','scripts/generate_shared_aot.py','scripts/generate_weather_batch.py']
    inputs += ['config/subtitle_hooks.json','recompiled/subtitle_events.hpp']
    inputs += [loop['implementation'] for loop in loops]
    inputs += [name for port in function_ports for name in (port['implementation'],port['adapter'],port['header'])]
    provenance.update({name: digest(ROOT/name) for name in inputs})
    report = dict(schema='wa2.shared-function-aot.v1',source_sha256=provenance,
                  existing_discovered_instructions=len(bodies),candidate_function_entries=len(records),
                  admitted_functions=len(admitted),native_function_bodies=len(merged),unique_instruction_locations=len(all_locations),
                  speed_optimized_function_entries=sorted(hot),
                  floating_neutral_leaf_callees=sorted(fp_neutral),
                  floating_neutral_leaf_contract=dict(closed_integer_only_cfg=True,no_calls_or_native_replacements=True,
                      caller_integer_barriers_retained=True,exception_unwind_commits_floating_state=True),
                  emitted_instruction_bodies=sum(len(merged[e]['locations']) for e in ordered),
                  direct_call_sites=len({pc for e in ordered for pc in merged[e]['locations']
                                         if any(x['kind']=='call_direct' for x in edges[pc])}),
                  guarded_jump_table_sites=[t['branch'] for t in used_tables],
                  fixed_width_memory_accesses=True,call_closure_proved_before_execution=True,
                  code_proof_bytes_share_initial_data=True,
                  compact_resumption_indices=True,resume_lookup_is_per_function_boundary_only=True,
                  dead_flag_updates_removed=len(dead_flags),dead_flag_operations=dict(Counter(dead_flags.values())),
                  flags_preserved_at_calls_returns_and_faults=True,
                  native_loops=loops,native_loops_have_no_instruction_meter=True,
                  native_function_ports=function_ports,native_function_ports_use_same_calling_abi=True,
                  partially_admitted_entries=[entry for entry in sorted(admitted)
                      if 'transcendental_contract_pending' in records[entry]['reasons']],
                  unsupported_instruction_sites=[dict(address=pc,reason='transcendental_contract_pending',
                      operation=PENDING_TRANSCENDENTAL.search(bodies[pc][0])[0]) for pc in unresolved],
                  unsupported_instruction_stops_preserve_prefix_state=True,
                  unsupported_instruction_results_verified=False,
                  functions=[dict(entry=e,**merged[e]) for e in ordered],deferred_functions=deferred,
                  deferred_reasons=dict(Counter(reason for r in deferred for reason in r['reasons'])),
                  whole_program_complete=False,original_cfg_whole_program_complete=graph['whole_program_complete'],
                  one_source_set_for_all_hosts=True,per_instruction_runtime_dispatch=False,
                  main_story_subtitle_events=dict(function_entries=sorted(subtitle_events),
                      return_locations=sum(len(e['return_locations']) for e in subtitle_events.values()),
                      original_functions_preserved=True,per_instruction_polling=False),
                  resumable_host_boundaries=True,indirect_targets_checked_against_static_entries=True,
                  original_candidate_entries=sorted(admitted),
                  runtime_decoder=False,jit=False,source_executable_opened=False,
                  complete_gameplay_verified=False)
    report['generated_sha256']={name:digest(OUT/name) for name in sources+['bodies.hpp','sources.cmake','semantic_boundaries.hpp']+oracle_sources}
    write('manifest.json', json.dumps(report, indent=2)+'\n')
    print(f'Shared AOT: {len(admitted)} functions, {len(all_locations)} instruction locations; '
          f'{len(deferred)} deferred function entries. Whole-game coverage is not proven.')


if __name__ == '__main__':
    main()
