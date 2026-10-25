"""Recover referenced callbacks from the existing read-only listing and C++.

No executable input, instruction decoder, new disassembly, or runtime fallback.
Exact templates are relocated first; new operand forms use the original emitter
with already-decoded listing operands, checked against the existing C++ corpus.
Private listing, generated bodies and addresses remain outside public source.
"""
from collections import defaultdict
import copy
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
CACHE = ROOT/'build/cached-listing'
OUT = ROOT/'generated/cached-callbacks'
RELATIVE = {'UNCONDITIONAL_CALL', 'CALL_TERMINATOR', 'UNCONDITIONAL_JUMP', 'CONDITIONAL_JUMP'}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write(path, text):
    if not path.exists() or path.read_text() != text:
        path.write_text(text, encoding='utf-8', newline='\n')


def extend(graph, bodies, provenance, root=ROOT):
    """Load only a supplement bound to this exact base inventory and inputs."""
    folder = root/'generated/cached-callbacks'
    manifest = json.loads((folder/'manifest.json').read_text())
    if not manifest['passed'] or manifest['source_executable_opened'] or manifest['decoder_used']:
        raise ValueError('Invalid cached callback recovery proof')
    for name, sha in manifest['source_sha256'].items():
        if digest(root/name) != sha:
            raise ValueError('Stale cached callback recovery: '+name)
    if len(bodies) != manifest['base_instruction_count']:
        raise ValueError('Cached callback base inventory changed')
    from generate_shared_aot import CASE, PROOF
    extra = json.loads((folder/'cfg.json').read_text())
    rows = {i['address']: i for i in extra['instructions']}
    added = {}
    for match in CASE.finditer((folder/'code.cpp').read_text()):
        pc = int(match[1], 16)
        proof = PROOF.match(match[2])
        if pc in bodies or pc in added or pc not in rows or proof is None:
            raise ValueError('Duplicate or invalid recovered callback instruction')
        raw = bytes(map(int, proof[1].split(',')))
        if raw.hex() != rows[pc]['bytes']:
            raise ValueError('Recovered CFG and C++ proof differ')
        added[pc] = (match[2][proof.end():].strip(), raw, match[2])
    if set(added) != set(rows):
        raise ValueError('Incomplete recovered callback source')
    bodies.update(added)
    graph['instructions'].extend(extra['instructions'])
    graph['instructions'].sort(key=lambda i: i['address'])
    graph['edges'].extend(extra['edges'])
    graph['function_candidates'] = sorted(set(graph['function_candidates']) | set(extra['function_candidates']))
    graph['cached_callback_audit'] = {k: v for k, v in manifest.items() if k != 'source_sha256'}
    provenance.update(manifest['source_sha256'])
    provenance['generated/cached-callbacks/manifest.json'] = digest(folder/'manifest.json')


def main():
    from cached_operands import parse, serialize
    import generate_shared_aot as a
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.GetCurrentProcess.restype = ctypes.c_void_p
    kernel.SetPriorityClass.argtypes = [ctypes.c_void_p, ctypes.c_uint]
    if not kernel.SetPriorityClass(kernel.GetCurrentProcess(), 0x4000):
        raise ctypes.WinError(ctypes.get_last_error())
    kernel.SetErrorMode(0x8003)
    flags = subprocess.CREATE_NO_WINDOW | subprocess.BELOW_NORMAL_PRIORITY_CLASS
    environment = os.environ.copy()
    environment.update(TEMP=str(ROOT/'build/temp'), TMP=str(ROOT/'build/temp'),
        ZIG_GLOBAL_CACHE_DIR=str(ROOT/'build/zig-cache'), ZIG_LOCAL_CACHE_DIR=str(ROOT/'build/zig-local'))
    CACHE.mkdir(parents=True, exist_ok=True)
    OUT.mkdir(parents=True, exist_ok=True)
    def run(command, label):
        with (ROOT/'logs'/('cached-callback-'+label+'.log')).open('wb') as log:
            child = subprocess.Popen(list(map(str, command)), cwd=ROOT, env=environment,
                creationflags=flags, stdout=log, stderr=subprocess.STDOUT)
            try:
                code = child.wait(180)
            except subprocess.TimeoutExpired:
                subprocess.run(['taskkill','/PID',str(child.pid),'/T','/F'], creationflags=flags,
                    stdout=log, stderr=subprocess.STDOUT, timeout=15)
                child.wait(15)
                raise
            if code:
                raise RuntimeError('Cached callback '+label+' failed; inspect log')

    graph, bodies, edges, records, admitted, provenance = a.analyze(include_cached_callbacks=False)
    original = {i['address']: i for i in graph['instructions']}
    live = {pc for entry in admitted for pc in records[entry]['locations']}
    native = {f['start'] for f in json.loads((ROOT/'port/native_functions.json').read_text())['functions']}
    retired = json.loads((ROOT/'port/retired_modules.json').read_text())['modules']
    retired_code = [(int(m['start'],0), int(m['end'],0)) for m in retired]
    retired_metadata = [(int(m['start'],0), int(m['end'],0)) for item in retired for m in item['callback_metadata']]
    listing = json.loads((CACHE/'listing.json').read_text())
    if any(listing.get(k) for k in ('source_executable_opened','new_analysis_performed','decompiler_used','program_modified')):
        raise ValueError('Listing must come from the existing read-only analysis database')
    cached = {i['address']: i for i in listing['instructions']}
    functions = {f['address']: f for f in listing['functions']}
    sections = {}
    for name, base in [('text',0x401000),('rdata',0x4a1000),('data',0x4be000)]:
        sections[base] = bytes(int(v) for v in (ROOT/f'generated/static-data/{name}.inc').read_text().split(',') if v.strip())
    def initial(pc, size):
        for base, data in sections.items():
            if base <= pc and pc+size <= base+len(data):
                return data[pc-base:pc-base+size]
        raise ValueError(f'Cached address outside initialized sections: {pc:#x}')
    def retired_target(pc):
        return any(lo <= pc < hi for lo, hi in retired_code)
    roots = defaultdict(set)
    for target, function in functions.items():
        if target in live or target in native or retired_target(target):
            continue
        for ref in function['references']:
            source = ref['from']
            if ref['type'] != 'DATA' or any(lo <= source < hi for lo, hi in retired_metadata):
                continue
            # An exact cached DATA reference plus an initialized pointer proves
            # table membership. This also handles unaligned and in-text tables.
            try:
                if int.from_bytes(initial(source,4),'little') == target:
                    roots[target].add(source)
            except ValueError:
                pass
    for pc in live:
        for target in cached.get(pc,{}).get('code_references',[]):
            # DATA references into .text can also be switch tables, not code.
            # Only an already decoded instruction boundary proves an entrance.
            if target in cached and target not in live and target not in native:
                if retired_target(target):
                    raise ValueError(f'Active reference into a retired module: {target:#x}')
                roots[target].add(pc)

    exact = defaultdict(list)
    relative = defaultdict(list)
    for pc, (_, raw, _) in bodies.items():
        rel = [o for o in original[pc]['operands'] if o.get('relative')]
        if rel:
            if len(rel) != 1:
                raise ValueError('Multiple relative operands in checked template')
            width = rel[0]['width']//8
            relative[(len(raw), raw[:-width], width)].append(pc)
        else:
            exact[raw].append(pc)
    def template(record):
        raw = bytes.fromhex(record['bytes'])
        if record['flow_type'] in RELATIVE:
            if len(record['flows']) != 1:
                raise ValueError('Ambiguous cached direct target')
            for width in (1,2,4):
                if (record['address']+len(raw)+int.from_bytes(raw[-width:],'little',signed=True))&0xffffffff != record['flows'][0]:
                    continue
                for source in relative.get((len(raw),raw[:-width],width),[]):
                    old_next = source+len(raw)
                    targets = {e['to'] for e in edges[source] if e['kind'] in ('call_direct','jump_direct','branch_taken')}
                    if len(targets)==1 and old_next not in targets:
                        return source
            return None
        return next(iter(exact.get(raw,[])), None)
    todo = list(roots)
    entries = set(roots)
    visited = set()
    selected = {}
    bridge_templates = {}
    synthetic = {}
    listing_gap_templates = {}
    catch_sites = {pc for f in functions.values() if f['name'].startswith('Catch_All')
                   for lo,hi in f['ranges'] for pc in range(lo,hi+1)}
    def recover_catch_continuation(target):
        # Catch funclets return a continuation in EAX. Ghidra can leave its
        # register-restore prefix as data. Require a unique concatenation of
        # whole checked fallthrough templates ending at a cached code boundary.
        options = {raw:(sources[0],False) for raw,sources in exact.items()
                   if {e['kind'] for e in edges[sources[0]]} == {'fallthrough'}}
        for source, record in cached.items():
            raw=bytes.fromhex(record['bytes'])
            if raw in options or record['flow_type']!='FALL_THROUGH':
                continue
            try:
                parse(record)
            except ValueError:
                continue
            options[raw]=(source,True)
        paths = []
        def walk(pc, path):
            if len(paths)>1 or pc-target>32:
                return
            if pc in cached:
                paths.append(path)
                return
            for raw,(source,from_listing) in options.items():
                end = pc+len(raw)
                if end-target<=32 and initial(pc,len(raw))==raw:
                    walk(end,path+[(pc,source,raw,from_listing)])
        walk(target,[])
        if len(paths)!=1 or not paths[0]:
            raise ValueError(f'Unproven catch continuation: {target:#x}')
        for pc,source,raw,from_listing in paths[0]:
            if from_listing:
                synthetic[pc]=copy.deepcopy(cached[source])
                synthetic[pc].update(address=pc,fallthrough=pc+len(raw))
                listing_gap_templates[pc]=source
            else:
                synthetic[pc] = dict(address=pc,size=len(raw),bytes=raw.hex(),text=original[source]['text'],
                    mnemonic=original[source]['mnemonic'],operand_text=[],flows=[],flow_type='FALL_THROUGH')
                bridge_templates[pc] = source
    while todo:
        pc = todo.pop()
        if pc in visited or pc in live or pc in native:
            continue
        if retired_target(pc):
            raise ValueError(f'Active call into unregistered retired code: {pc:#x}')
        visited.add(pc)
        if pc in original:
            for edge in edges[pc]:
                target = edge['to']
                if target is not None and edge['kind'] not in ('call_import','jump_import'):
                    todo.append(target)
                    if edge['kind']=='call_direct' and target not in live and target not in native:
                        entries.add(target)
            continue
        record = cached.get(pc, synthetic.get(pc))
        if record is None:
            # A cached no-return annotation can hide a post-CALL cleanup or
            # trap. Accept only these complete, existing byte-identical forms
            # with a proven following listing boundary (or the trap itself).
            for raw in (b'\xcc', b'\x83\xc4\x14'):
                if initial(pc,len(raw)) != raw or raw not in exact:
                    continue
                if raw != b'\xcc' and pc+len(raw) not in cached:
                    continue
                source = exact[raw][0]
                record = dict(address=pc,size=len(raw),bytes=raw.hex(),text=original[source]['text'],
                    mnemonic=original[source]['mnemonic'],operand_text=[],flows=[],
                    flow_type='TERMINATOR' if raw==b'\xcc' else 'FALL_THROUGH')
                bridge_templates[pc] = source
                break
        if record is None:
            raise ValueError(f'No cached instruction or exact bounded template: {pc:#x}')
        raw = bytes.fromhex(record['bytes'])
        if len(raw)!=record['size'] or initial(pc,len(raw))!=raw:
            raise ValueError(f'Cached listing and initial code bytes differ: {pc:#x}')
        selected[pc] = record
        flow = record['flow_type']
        if flow in RELATIVE:
            todo.extend(record['flows'])
            if flow in ('UNCONDITIONAL_CALL','CALL_TERMINATOR'):
                entries.update(t for t in record['flows'] if t not in live and t not in native)
            if flow != 'UNCONDITIONAL_JUMP':
                todo.append(pc+len(raw))
        elif flow in ('FALL_THROUGH','COMPUTED_CALL','COMPUTED_CALL_TERMINATOR'):
            todo.append(pc+len(raw))
        elif flow == 'COMPUTED_JUMP':
            if record['flows']:
                raise ValueError('New computed jump needs an explicit jump-table proof')
        elif flow != 'TERMINATOR':
            raise ValueError('Unsupported cached flow: '+flow)
        for target in record.get('code_references',[]):
            if target in cached and target not in live and target not in native:
                entries.add(target)
                todo.append(target)
            elif (target not in cached and target not in live and pc in catch_sites and
                  record['mnemonic']=='MOV' and record['operand_text'][0]=='EAX' and
                  cached.get(pc+len(raw),{}).get('mnemonic')=='RET'):
                recover_catch_continuation(target)
                entries.add(target)
                todo.append(target)

    templates = {pc: bridge_templates.get(pc,template(i)) for pc,i in selected.items()}
    models = {pc: parse(selected[pc]) for pc,source in templates.items() if source is None}
    base = ROOT/'tools/WindowsRecompTool-main'
    zydis = base/'third_party/zydis'
    helper = CACHE/'emit_cached_operands.exe'
    run([ROOT/'tools/python/ziglang/zig.exe','c++','-target','x86_64-windows-gnu','-std=c++20','-O1',
        '-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-DZYDIS_STATIC_BUILD','-DZYCORE_STATIC_BUILD',
        '-I'+str(ROOT),'-I'+str(base/'include'),'-I'+str(zydis/'include'),'-I'+str(zydis/'dependencies/zycore/include'),
        ROOT/'scripts/analysis/emit_cached_operands.cpp',ROOT/'build/winrecomp/third_party/zydis/libZydis.a',
        ROOT/'build/winrecomp/third_party/zydis/zycore/libZycore.a','-o',helper], 'emitter-build')
    def render(items, name):
        source = CACHE/(name+'.txt')
        output = CACHE/(name+'.cpp')
        write(source,''.join(serialize(i) for i in items))
        run([helper,source,output],name)
        return {int(m[1],16):m[2].strip() for m in a.CASE.finditer(output.read_text())}
    controls = []
    for pc, i in cached.items():
        if pc not in bodies or i['bytes'] != original[pc]['bytes']:
            continue
        try:
            controls.append(parse(i))
        except ValueError:
            pass
    control_bodies = render(controls,'control')
    if len(control_bodies)!=len(controls) or len(controls)<90000:
        raise ValueError('Incomplete cached operand control corpus')
    for pc, body in control_bodies.items():
        if body != bodies[pc][0]:
            raise ValueError(f'Cached operand emitter differs from checked C++: {pc:#x}')
    emitted = render(models.values(),'new')
    if set(emitted)!=set(models):
        raise ValueError('Incomplete operand emission')
    rows, following, cpp = [], [], []
    for pc, i in sorted(selected.items()):
        raw = bytes.fromhex(i['bytes'])
        next_pc = pc+len(raw)
        source = templates[pc]
        if source is None:
            row = copy.deepcopy(models[pc])
            body = emitted[pc]
            new_edges = [dict(from_=pc,to=next_pc,kind='fallthrough')]
        else:
            row = copy.deepcopy(original[source])
            body = bodies[source][0]
            new_edges = [dict(from_=pc,to=e['to'],kind=e['kind']) for e in edges[source]]
            changes = {source+len(raw):next_pc}
            if i['flow_type'] in RELATIVE:
                target, = i['flows']
                old_target, = {e['to'] for e in edges[source] if e['kind'] in ('call_direct','jump_direct','branch_taken')}
                changes[old_target] = target
                for op in row['operands']:
                    if op.get('relative'):
                        op['value_hex'] = hex((target-next_pc)&0xffffffff)
            kinds = {e['kind'] for e in edges[source]}
            # Relocate only control literals, never data immediates that happen
            # to equal a PC. The existing emitter's transfer syntax is checked.
            if i['flow_type'] in RELATIVE:
                body = re.sub(r'0x([0-9a-f]+)u',lambda m: f'0x{changes.get(int(m[1],16),int(m[1],16)):x}u',body)
            elif kinds == {'fallthrough'}:
                tail = f's.eip=0x{source+len(raw):x}u;'
                if not body.endswith(tail):
                    raise ValueError('Invalid fallthrough template')
                body = body[:-len(tail)]+f's.eip=0x{next_pc:x}u;'
            elif 'call_continuation' in kinds:
                tail = f'wr::push(s,m,0x{source+len(raw):x}u);'
                if tail not in body:
                    raise ValueError('Invalid indirect call template')
                body = body.replace(tail,f'wr::push(s,m,0x{next_pc:x}u);')
            for edge in new_edges:
                if edge['kind'] in ('fallthrough','call_continuation','branch_not_taken','call_direct','jump_direct','branch_taken'):
                    edge['to'] = changes[edge['to']]
        row.update(address=pc,size=len(raw),bytes=raw.hex(),text=i['text'])
        rows.append(row)
        following.extend({'from':e['from_'],'to':e['to'],'kind':e['kind'],'detail':'checked cached callback'} for e in new_edges)
        proof = 'wr::check_code(m,s.eip,{'+','.join(map(str,raw))+'});\n'
        cpp.append(f'case 0x{pc:x}u: {{ '+proof+body+'\nreturn true; }\n')
    write(OUT/'code.cpp',''.join(cpp))
    write(OUT/'cfg.json',json.dumps(dict(instructions=rows,edges=following,function_candidates=sorted(entries)),indent=2)+'\n')
    inputs = list(provenance)+['logs/cfg.json','scripts/generate_shared_aot.py',
        'scripts/recover_cached_callbacks.py','scripts/cached_operands.py','scripts/analysis/emit_cached_operands.cpp',
        'scripts/analysis/ExportCachedListing.java','build/cached-listing/listing.json',
        'tools/WindowsRecompTool-main/src/lift.cpp','tools/WindowsRecompTool-main/include/winrecomp/core.hpp',
        'port/native_functions.json','port/retired_modules.json','generated/static-data/text.inc',
        'generated/static-data/rdata.inc','generated/static-data/data.inc',
        'generated/cached-callbacks/code.cpp','generated/cached-callbacks/cfg.json']
    report = dict(schema='wa2.cached-callback-audit.v1',passed=True,source_executable_opened=False,decoder_used=False,
        new_analysis_performed=False,runtime_fallback_added=False,base_instruction_count=len(bodies),
        roots=[dict(entry=t,name=functions.get(t,{}).get('name','cached label'),references=sorted(v)) for t,v in sorted(roots.items())],
        restored_entries=sorted(entries),restored_instruction_sites=len(rows),exact_template_sites=len(rows)-len(models),
        cached_operand_sites=len(models),existing_cpp_roundtrip_checks=len(controls),
        bounded_gap_templates=[dict(address=pc,template=source) for pc,source in sorted(bridge_templates.items())],
        cached_listing_gap_templates=[dict(address=pc,template=source) for pc,source in sorted(listing_gap_templates.items())],
        unresolved_referenced_entries=[],source_sha256={name:digest(ROOT/name) for name in sorted(set(inputs))})
    write(OUT/'manifest.json',json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k not in ('source_sha256','roots','restored_entries')},indent=2))


if __name__ == '__main__':
    main()
