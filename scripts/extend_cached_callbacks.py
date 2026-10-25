"""Admit an omitted script callback using cached analysis and checked C++ only.

This is a bounded relocation of existing instruction templates, not a decoder.
The complete resulting signature must match the prior build's data-only cache.
No source executable is opened, and no runtime fallback is introduced.
"""
import copy
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ENTRY = 0x456650
CALLEE = 0x40af20
# The four callbacks registered from the separate, null-terminated extension
# table are not part of the dispatcher's 128 built-in command slots.
EXTENSION_TABLE = 0x4c27c0
EXTENSION_ENTRIES = (ENTRY, 0x456680, 0x456690, 0x4566b0)
# Each address names an already checked instruction C++/CFG template. Only
# fallthrough PCs and the CALL return/displacement are relocated. Registers,
# memory accesses, flag semantics and fault ordering stay exactly as emitted.
TEMPLATES = (
    0x455bc0, 0x455bc1, 0x454f82, 0x4553ae, 0x454c1a,
    0x455bd0, 0x414704, 0x455bd1, 0x455bd2, 0x455bd0,
    0x455bd3, 0x455bd8, 0x455bdb, 0x455bdd, 0x455bdf,
    0x455be0, 0x455be2, 0x455be7, 0x455be8,
)
SIGNATURE = bytes.fromhex('568b7424088b46548b4e408b562c508b4618515250e8b648fbff83c410f7d81bc0408906b8010000005ec3')
ANALYSIS_SHA256 = '1b94d98ffef4fdf9c8eda70756fc7ed43309a1b9ee4a56ff6a7ab97c4609b1e0'


def extend(graph, bodies, provenance, root=ROOT):
    inputs = ['scripts/extend_cached_callbacks.py',
              'build/source-analysis/functions-typed/00456650.c',
              'build/source-analysis/functions-typed/00456650.json',
              'generated/static-data/text.inc', 'generated/static-data/data.inc']
    payloads = {name: (root/name).read_bytes() for name in inputs}
    hashes = {name: hashlib.sha256(data).hexdigest() for name, data in payloads.items()}
    analysis = json.loads(payloads[inputs[2]])
    if (hashes[inputs[1]] != ANALYSIS_SHA256 or analysis['c_sha256'] != ANALYSIS_SHA256 or
            analysis['address'] != ENTRY or analysis['body_bytes'] != len(SIGNATURE) or
            analysis['last_address'] != ENTRY+len(SIGNATURE)-1 or analysis['calls'] != [CALLEE] or analysis['warnings'] or
            analysis['references'] != [{'from': EXTENSION_TABLE, 'type': 'DATA'}]):
        raise ValueError('Cached script wait callback analysis changed')
    def cached_bytes(name):
        return bytes(int(value) for value in payloads[name].decode('ascii').split(',') if value.strip())
    text = cached_bytes(inputs[3])
    data = cached_bytes(inputs[4])
    if text[ENTRY-0x401000:ENTRY-0x401000+len(SIGNATURE)] != SIGNATURE:
        raise ValueError('Cached script wait callback code signature changed')
    table_at = EXTENSION_TABLE-0x4be000
    if len(data) < table_at+20:
        raise ValueError('Cached script extension callback table is truncated')
    table = tuple(int.from_bytes(data[table_at+i*4:table_at+i*4+4], 'little') for i in range(5))
    if table != EXTENSION_ENTRIES+(0,):
        raise ValueError('Cached script extension callback table changed')
    instructions = {i['address']: i for i in graph['instructions']}
    edges = {}
    for edge in graph['edges']:
        edges.setdefault(edge['from'], []).append(edge)
    if ENTRY in bodies or any(pc < ENTRY+len(SIGNATURE) and pc+row['size'] > ENTRY for pc, row in instructions.items()):
        raise ValueError('Script wait callback overlaps the cached CFG; retire the extension explicitly')
    added = []
    at = ENTRY
    for source in TEMPLATES:
        body, raw, _ = bodies[source]
        following = edges[source]
        kinds = {edge['kind'] for edge in following}
        next_pc = at+len(raw)
        if kinds == {'call_direct', 'call_continuation'}:
            if {e['to'] for e in following if e['kind'] == 'call_direct'} != {CALLEE}:
                raise ValueError('Script wait template callee changed')
            expected = f'{{auto target=0x{CALLEE:x}u;wr::push(s,m,0x{source+len(raw):x}u); s.eip=target;}}'
            if body != expected or raw != b'\xe8'+((CALLEE-source-5)&0xffffffff).to_bytes(4, 'little'):
                raise ValueError('Script wait CALL template changed')
            body = expected.replace(f'0x{source+len(raw):x}u', f'0x{next_pc:x}u')
            raw = b'\xe8'+((CALLEE-next_pc)&0xffffffff).to_bytes(4, 'little')
            new_edges = [dict(from_=at, to=CALLEE, kind='call_direct'),
                         dict(from_=at, to=next_pc, kind='call_continuation')]
        elif kinds == {'return'}:
            if source != TEMPLATES[-1] or body != 's.eip=wr::pop(s,m);' or raw != b'\xc3':
                raise ValueError('Script wait return template changed')
            new_edges = [dict(from_=at, to=None, kind='return')]
        else:
            tail = f's.eip=0x{source+len(raw):x}u;'
            if (len(following) != 1 or following[0]['to'] != source+len(raw) or
                    following[0]['kind'] != 'fallthrough' or not body.endswith(tail)):
                raise ValueError(f'Script wait fallthrough template changed: {source:#x}')
            body = body[:-len(tail)]+f's.eip=0x{next_pc:x}u;'
            new_edges = [dict(from_=at, to=next_pc, kind='fallthrough')]
        if raw != SIGNATURE[at-ENTRY:next_pc-ENTRY]:
            raise ValueError(f'Script wait template signature differs: {at:#x}')
        row = copy.deepcopy(instructions[source])
        row.update(address=at, size=len(raw), bytes=raw.hex())
        if kinds == {'call_direct', 'call_continuation'}:
            row['operands'] = [dict(width=32, kind='immediate', relative=True, signed=True,
                                   value_hex=hex((CALLEE-next_pc)&0xffffffff))]
        proof = 'wr::check_code(m,s.eip,{'+','.join(map(str, raw))+'});\n'
        added.append((at, row, (body, raw, proof+body), new_edges))
        at = next_pc
    if at != ENTRY+len(SIGNATURE):
        raise ValueError('Incomplete cached script wait callback')
    # Commit the extension only after every template and the full byte range
    # have been validated. The old CFG/cache files are never rewritten.
    for pc, row, body, following in added:
        bodies[pc] = body
        graph['instructions'].append(row)
        graph['edges'].extend({'from': e['from_'], 'to': e['to'], 'kind': e['kind'],
                               'detail': 'cached script wait callback template'} for e in following)
    graph['function_candidates'].append(ENTRY)
    graph['instructions'].sort(key=lambda i: i['address'])
    provenance.update(hashes)
    graph['cached_callback_extensions'] = dict(entry=ENTRY, instructions=len(added), bytes=len(SIGNATURE),
        callee=CALLEE, table=EXTENSION_TABLE, extension_entries=list(EXTENSION_ENTRIES),
        template_addresses=list(TEMPLATES), source_executable_opened=False, decoder_used=False)
