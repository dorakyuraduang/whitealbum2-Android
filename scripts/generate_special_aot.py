"""Generate one Special Contents C++ source set for Windows and Android.

Consumes the verified, local-only preparation cache, never an executable.
Uses the existing shared AOT control-flow, integer, memory and FP transforms.
No main-story address hooks, UI patches or subtitles apply.
"""
from __future__ import annotations
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import re

from generate_shared_aot import (CASE, PROOF, HELPERS, PENDING_TRANSCENDENTAL,
    CONTRACT_FAULT, emit_body, fixed_memory, portable_floating, merge_bodies,
    dead_flag_operations, register_floating, register_integers)
from prepare_special_contents import ROOT, digest, write_json

OUT = ROOT/'generated/special-contents/aot'


def write(name, value):
    path = OUT/name
    if not path.exists() or path.read_text(encoding='utf-8') != value:
        path.write_text(value, encoding='utf-8', newline='\n')


def analyze():
    cache = ROOT/'generated/special-contents'
    receipt = json.loads((cache/'preparation.json').read_text())
    profile = json.loads((ROOT/'config/special_contents.json').read_text())
    if receipt['input_sha256'] != profile['sha256']:
        raise ValueError('Special cached identity differs from selected program')
    for name, expected in receipt['cache_sha256'].items():
        if digest(ROOT/name) != expected:
            raise ValueError('Special preparation cache changed: '+name)
    from audit_special_callbacks import validated_cache
    callbacks=validated_cache()
    graph = json.loads((cache/'callback-upgrade/cfg.json').read_text())
    if graph['diagnostics']:
        raise ValueError('Special cached discovery has diagnostics')
    instructions = {i['address']: i for i in graph['instructions']}
    bodies = {}
    for path in sorted((ROOT/callbacks['raw_directory']).glob('code_page_*.cpp')):
        for match in CASE.finditer(path.read_text()):
            pc = int(match[1], 16)
            proof = PROOF.match(match[2])
            if proof is None or pc in bodies:
                raise ValueError(f'invalid Special oracle at {pc:#x}')
            raw = bytes(map(int, proof[1].split(',')))
            if instructions[pc]['bytes'] != raw.hex():
                raise ValueError(f'Special CFG/C++ proof differs at {pc:#x}')
            body = match[2][proof.end():].strip()
            if set(re.findall(r'wr::(\w+)', body))-HELPERS:
                raise ValueError(f'unported Special instruction semantics at {pc:#x}')
            bodies[pc] = (body, raw, match[2])
    if bodies.keys() != instructions.keys():
        raise ValueError('Special instruction inventories differ')
    edges = defaultdict(list)
    for edge in graph['edges']:
        edges[edge['from']].append(edge)
    records = {}
    seeds = set(graph['function_candidates']) | set(graph['roots'])
    # Every instruction must have a body. A remainder can be an otherwise
    # unreferenced static jump-table destination, not an invented entry.
    covered = set()
    while True:
        for entry in sorted(seeds-records.keys()):
            seen, calls, pending = set(), set(), [entry]
            while pending:
                pc = pending.pop()
                if pc in seen:
                    continue
                if pc not in bodies:
                    raise ValueError(f'missing Special direct destination {pc:#x}')
                seen.add(pc)
                for edge in edges[pc]:
                    kind, target = edge['kind'], edge['to']
                    if kind == 'call_direct':
                        calls.add(target)
                    elif kind in ('return', 'stop', 'call_indirect', 'jump_indirect',
                                  'call_import', 'jump_import', 'data_callback_initial'):
                        pass
                    elif target is not None:
                        pending.append(target)
                    else:
                        raise ValueError(f'unknown Special edge at {pc:#x}: {kind}')
                if len(seen) > 65535:
                    raise ValueError('Special merged function exceeds checkpoint capacity')
            records[entry] = dict(locations=sorted(seen), calls=sorted(calls), reasons=[])
            covered.update(seen)
        remaining = set(bodies)-covered
        if not remaining:
            break
        seeds.add(min(remaining))
    merged, owners = merge_bodies(records, set(records))
    for pc in bodies:
        for edge in edges[pc]:
            if edge['kind'] == 'call_direct' and edge['to'] not in owners:
                raise ValueError('Unresolved Special direct call')
    return graph, bodies, edges, merged, owners, receipt


def image_source(graph, data):
    metadata = graph['image']
    cpp = ['#include <winrecomp/image.hpp>', '#include <algorithm>',
           'namespace wa2::special {', 'extern const std::uint8_t image_bytes[]={',
           '#include "image.inc"', '};', 'wr::Image initial_image(){wr::Image image;']
    cpp += ['image.static_input_identity='+json.dumps(hashlib.sha256(data).hexdigest())+';',
            f'image.base={metadata["image_base"]}u;image.entry={metadata["entry"]}u;image.size={metadata["image_size"]}u;',
            f'image.headers_size={min(s["raw_offset"] for s in metadata["sections"])}u;',
            'image.bytes.assign(std::begin(image_bytes),std::end(image_bytes));']
    for section in metadata['sections']:
        cpp.append('image.sections.push_back({'+json.dumps(section['name'])+','+','.join(str(section[k])+'u' for k in ('rva','virtual_size','raw_offset','raw_size','characteristics'))+'});')
    for index, item in enumerate(metadata['directories']):
        cpp.append(f'image.directories[{index}]={{{item["rva"]}u,{item["size"]}u}};')
    for item in metadata['imports']:
        ordinal = str(item['ordinal']) if item['ordinal'] is not None else 'std::nullopt'
        cpp.append('image.imports.push_back({'+json.dumps(item['dll'])+','+json.dumps(item['name'])+','+str(item['iat'])+'u,'+ordinal+'});')
    for kind in ('exports', 'tls_callbacks'):
        for entry in metadata[kind]:
            cpp.append(f'image.{kind}.insert({entry}u);')
    cpp += ['return image;}', '}']
    write('image.cpp', '\n'.join(cpp)+'\n')
    write('image.inc', '\n'.join(','.join(map(str, data[at:at+32]))+',' for at in range(0, len(data), 32))+'\n')


def generate():
    from verify_x87_transcendental import fresh as verified_transcendental
    math_proof=verified_transcendental()
    graph, bodies, edges, merged, owners, receipt = analyze()
    contracts=json.loads((ROOT/'config/special_native_contracts.json').read_text())
    codec_entries=[int(contracts['vorbis'][key],0) for key in ('construct','reset','format','decode')]
    if len(set(codec_entries))!=4 or any(pc not in owners for pc in codec_entries):
        raise ValueError('Missing distinct Special codec ABI functions')
    draw_fields=('sprite_entry','glyph_entry','globals','images','palette','half_pixel','sse2',
                 'radians','cosine_words','sine_words','font_ranges','font_styles','first_weather','trig_dispatch')
    drawing={key:int(contracts['drawing'][key],0) for key in draw_fields}
    draw_entries={drawing['sprite_entry']:'draw_sprite',drawing['glyph_entry']:'draw_glyph'}
    if set(draw_entries)!={0x43cb90,0x447d70} or any(pc not in merged for pc in draw_entries):
        raise ValueError('Special drawing profile must name the verified source ABI entries')
    sort_fields=('begin','end','objects','mask','frame_register','pointer','keys','ids','first','last')
    sorting={key:int(contracts['sort'][key],0) if isinstance(contracts['sort'][key],str) else contracts['sort'][key] for key in sort_fields}
    sort_owner=owners.get(sorting['begin'])
    if sorting['begin']!=0x443c90 or sorting['end']!=0x443d7c or sort_owner!=owners.get(sorting['end']):
        raise ValueError('Special sorting prefix must retain its verified owning function and continuation')
    # The original bodies stay available to finite memory reference checks.
    # Production calls unwind before entering the shared codec ABI adapter.
    call_owners={pc:owner for pc,owner in owners.items() if pc not in codec_entries}
    hot_config=json.loads((ROOT/'config/special_hot_functions.json').read_text())
    hot=set(hot_config['entries'])
    if (hot_config['input_sha256']!=receipt['input_sha256'] or
            (hot_config['shared_optimization'],hot_config['other_generated_functions'])!=('O2','Oz') or
            len(hot)!=len(hot_config['entries']) or not hot.issubset(merged) or
            set(hot_config['register_floating_point_entries'])!=hot or
            set(hot_config['register_integer_entries'])!=hot):
        raise ValueError('Special measured hot-function contract differs from the common compiler policy')
    OUT.mkdir(parents=True, exist_ok=True)
    ordered = sorted(merged)
    positions = {pc: index for record in merged.values() for index, pc in enumerate(record['locations'])}
    dead_flags = {pc: op for record in merged.values() for pc, op in dead_flag_operations(record, bodies, edges).items()}
    header = ['#pragma once', '#include "functions.hpp"', '#include "integer_value.hpp"',
              '#include "native_string.hpp"', '#include "work_samples.hpp"', '#include "special/lzss.hpp"',
              '#include "fp_registers.hpp"', '#include "gpr_registers.hpp"',
              'namespace wa2::special {', 'using aot::Context;using aot::Scope;',
              'using aot::CodeSpan;using aot::Function;using aot::Location;',
              'using aot::call_leaf_service;using aot::call_source_indirect;']
    for entry in ordered:
        parameters='bool Metered,bool NativeRegisters=true' if entry in hot else 'bool Metered'
        header += [f'template<{parameters}> void f_{entry:x}(Context&,wr::Memory&,Scope&,std::uint64_t&,std::uint32_t);']
    header += ['std::span<const Function> functions() noexcept;',
               'Location locate(std::uint32_t) noexcept;', 'wr::Image initial_image();', '}']
    # functions.hpp supplies Cpu/Memory, while Image is intentionally separate.
    header.insert(2, '#include <winrecomp/image.hpp>')
    write('bodies.hpp', '\n'.join(header)+'\n')
    write('render_profile.hpp','#pragma once\n#include "sprite_source.hpp"\nnamespace wa2::special {\n'+
          'inline constexpr wa2::aot::SpriteSource drawing_source{'+','.join(hex(drawing[key])+'u' for key in draw_fields)+'};\n}\n')
    write('sort_profile.hpp','#pragma once\n#include "source_sort.hpp"\nnamespace wa2::special {\n'+
          'inline constexpr wa2::aot::SortSource sorting_source{'+','.join(str(sorting[key]) for key in sort_fields)+'};\n}\n')
    write('native_entries.hpp','#pragma once\n#include <cstdint>\nnamespace wa2::special {\n'+
          'inline constexpr std::uint32_t codec_boundary_entries[]={'+','.join(hex(pc)+'u' for pc in codec_entries)+'};\n}\n')
    spans_by_entry = {}
    sample_indices={entry:index for index,entry in enumerate(ordered)}
    sources = []
    transcendental = sorted(pc for pc, (body, _, _) in bodies.items() if PENDING_TRANSCENDENTAL.search(body))
    pending = []
    shards=[(f'functions_{shard}.cpp',[entry for entry in ordered[shard::16] if entry not in hot]) for shard in range(16)]
    shards.append(('functions_hot.cpp',sorted(hot)))
    for name, entries in shards:
        sources.append(name)
        cpp = ['#include "bodies.hpp"', 'namespace wa2::special {']
        if set(entries)&(draw_entries.keys()|{sort_owner}):cpp.insert(1,'#include "special/render.hpp"')
        for entry in entries:
            record = merged[entry]
            parameters='bool Metered,bool NativeRegisters' if entry in hot else 'bool Metered'
            cpp += [f'template<{parameters}> void f_{entry:x}(Context& s,wr::Memory& m,Scope& scope,std::uint64_t& budget,std::uint32_t resume){{',
                    f'wa2::aot::FunctionWorkTimer work_timer({sample_indices[entry]}u,0,0,s.fp.get16(0));']
            if entry in hot:
                # Both paths use the same arithmetic and source control flow.
                # The serialized specialization is a finite memory-test oracle.
                cpp += ['wa2::aot::fp::RegisterFrame<Metered || !NativeRegisters> floating(s,m,scope);',
                        'wa2::aot::IntegerRegisters<Metered || !NativeRegisters> integer(s,m,scope);']
            cpp += [
                    'switch(resume){', *[f'case {positions[pc]}u:goto L_{pc:x};' for pc in record['locations']],
                    'default:throw wr::GuestFault(wr::FaultKind::untranslated,s.eip,"invalid Special resume entry");}']
            spans = []
            for pc in record['locations']:
                body, raw, _ = bodies[pc]
                native = CONTRACT_FAULT if pc in pending else emit_body(pc, body, edges, call_owners, positions)
                if pc==0x4382a6:
                    native='if constexpr(!Metered)wa2::special::try_lzss_prefix(s,m);\n'+native
                if pc in dead_flags:
                    operation = dead_flags[pc]
                    native = native.replace('wr::'+operation+'(', f'wa2::aot::value::{operation}<Metered>(')
                # Raw instruction oracles default to diagnostic REP metering.
                # Match the enclosing static body's policy on every string
                # operation, including conditional/segmented fallbacks.
                native = fixed_memory(portable_floating(native.replace(
                    'wr::string_op(', 'wr::aot_string_op<Metered>(')))
                if entry in hot:
                    native=register_integers(register_floating(native))
                cpp += [f'L_{pc:x}:{{s.eip=0x{pc:x}u;', 'if constexpr(Metered)wr::tick(budget,s.eip);']
                if pc in draw_entries:
                    invocation=f'wa2::special::{draw_entries[pc]}(s,m)'
                    if entry in hot:
                        invocation='floating.external([&]{return integer.external([&]{return '+invocation+';});})'
                    cpp += [f'if constexpr(!Metered)if({invocation})return;']
                if pc==sorting['begin']:
                    invocation='wa2::special::sort_objects(s,m)'
                    if entry in hot:invocation='floating.external([&]{return integer.external([&]{return '+invocation+';});})'
                    policy='!Metered && NativeRegisters' if entry in hot else '!Metered'
                    cpp += [f'if constexpr({policy})if({invocation})goto L_{sorting["end"]:x};']
                cpp += [native,'}']
                if spans and spans[-1][0]+len(spans[-1][1]) == pc:
                    spans[-1][1].extend(raw)
                else:
                    spans.append((pc, bytearray(raw)))
            cpp += ['}']
            if entry in hot:
                cpp += [f'template void f_{entry:x}<false,{native}>(Context&,wr::Memory&,Scope&,std::uint64_t&,std::uint32_t);' for native in ('false','true')]
            else:
                cpp += [f'template void f_{entry:x}<false>(Context&,wr::Memory&,Scope&,std::uint64_t&,std::uint32_t);']
            spans_by_entry[entry] = spans
        cpp.append('}')
        write(name, '\n'.join(cpp)+'\n')
    # A compact interval/start-bit table validates exact instruction boundaries.
    # It has no opcode decoder, per-instruction dispatch, or interpreter.
    data = (ROOT/'generated/special-contents/image.bin').read_bytes()
    image_source(graph, data)
    cpp = ['#include "bodies.hpp"', '#include <algorithm>', '#include <bit>',
           'namespace wa2::special {', 'extern const std::uint8_t image_bytes[];', 'namespace {']
    sections = graph['image']['sections']
    ranges = []
    for index, entry in enumerate(ordered):
        spans = spans_by_entry[entry]
        code = []
        for n, (first, raw) in enumerate(spans):
            section, = [s for s in sections if graph['image']['image_base']+s['rva'] <= first < graph['image']['image_base']+s['rva']+s['raw_size']]
            offset = first-graph['image']['image_base']-section['rva']+section['raw_offset']
            if data[offset:offset+len(raw)] != raw:
                raise ValueError('Special initial memory differs from instruction proof')
            code.append(f'{{0x{first:x}u,{{image_bytes+{offset}u,{len(raw)}u}}}}')
            bitmap = [0]*((len(raw)+63)//64)
            for pc in merged[entry]['locations']:
                if first <= pc < first+len(raw):
                    bit = pc-first
                    bitmap[bit//64] |= 1 << (bit%64)
            ranks, rank = [], 0
            for word in bitmap:
                ranks.append(rank)
                rank += word.bit_count()
            cpp += [f'constexpr std::uint64_t bits_{index}_{n}[]={{'+','.join(hex(b)+'ull' for b in bitmap)+'};',
                    f'constexpr std::uint16_t ranks_{index}_{n}[]={{'+','.join(map(str, ranks))+'};']
            ranges.append((first, first+len(raw), index, positions[first], f'bits_{index}_{n}', f'ranks_{index}_{n}'))
        cpp.append(f'const CodeSpan code_{index}[]={{'+','.join(code)+'};')
        if merged[entry]['bound_callees']:
            cpp.append(f'constexpr std::uint32_t callees_{index}[]={{'+','.join(str(pc)+'u' for pc in merged[entry]['bound_callees'])+'};')
        cpp.append(f'void entry_{index}(Context& s,wr::Memory& m,Scope& scope,std::uint64_t& budget,bool,std::uint32_t resume){{f_{entry:x}<false>(s,m,scope,budget,resume);}}')
    cpp += ['const Function table[]={', *[f'{{0x{entry:x}u,entry_{i},code_{i},'+(f'callees_{i}' if merged[entry]['bound_callees'] else '{}')+'},' for i, entry in enumerate(ordered)],
            '};', 'struct Range {std::uint32_t first,end,index,ordinal;const std::uint64_t* bits;const std::uint16_t* ranks;};', 'const Range ranges[]={',
            *[f'{{{first}u,{end}u,{index}u,{ordinal}u,{bits},{ranks}}},' for first, end, index, ordinal, bits, ranks in sorted(ranges)],
            '};', '}', 'std::span<const Function> functions() noexcept{return table;}',
            'Location locate(std::uint32_t pc) noexcept{',
            'const auto* it=std::upper_bound(std::begin(ranges),std::end(ranges),pc,[](auto pc,const auto& r){return pc<r.first;});',
            'if(it==std::begin(ranges))return {};--it;if(pc>=it->end)return {};',
            'const auto offset=pc-it->first,word=offset/64,bit=offset%64;const auto bits=it->bits[word];',
            'if(!(bits&(std::uint64_t(1)<<bit)))return {};',
            'return {&table[it->index],it->ordinal+it->ranks[word]+std::popcount(bits&((std::uint64_t(1)<<bit)-1))};}', '}']
    write('table.cpp', '\n'.join(cpp)+'\n')
    sources += ['table.cpp', 'image.cpp']
    write('sources.cmake', 'set(WA2_SPECIAL_AOT_SOURCES\n'+''.join(f'  "${{CMAKE_CURRENT_LIST_DIR}}/{name}"\n' for name in sources)+')\n')
    # Small, closed integer functions form a finite independent memory oracle.
    probes = []
    for entry, record in merged.items():
        if record['calls'] or len(record['locations']) > 48 or entry != record['locations'][0]:
            continue
        text = '\n'.join(bodies[pc][0] for pc in record['locations'])
        if 'm.' in text or 's.fp' in text or re.search(r'wr_(?:x87|sse)|wr::(?:push|string_op|divide|multiply|bit_|segment_)', text):
            continue
        if any(e['kind'] not in {'fallthrough','branch_taken','branch_not_taken','jump_direct','return'} for pc in record['locations'] for e in edges[pc]):
            continue
        if any(e['to'] is not None and e['to'] <= pc for pc in record['locations'] for e in edges[pc] if e['kind'] != 'return'):
            continue
        if 'wr::pop(s,m)' not in text or text.count('wr::pop(') != 1 or re.search(r'wr::set_reg\(s,4,', text):
            continue
        probes.append(entry)
        if len(probes) == 24:
            break
    pcs = sorted({pc for entry in probes for pc in merged[entry]['locations']})
    oracle = ['#include <winrecomp/compiled.hpp>', 'namespace wa2::special {',
              'bool probe_oracle(wr::Cpu& s,wr::Memory& m){switch(s.eip){']
    oracle += [f'case 0x{pc:x}u:{{'+bodies[pc][2]+'return true;}' for pc in pcs]
    oracle += ['default:return false;}}', '}']
    write('probe_oracle.cpp', '\n'.join(oracle)+'\n')
    callbacks=json.loads((ROOT/'generated/special-contents/callback-upgrade/manifest.json').read_text())
    write('probe_entries.hpp', '#pragma once\n#include <cstdint>\nnamespace wa2::special {\ninline constexpr std::uint32_t probe_entries[]={'+','.join(str(pc)+'u' for pc in probes)+'};\n'+
        'inline constexpr std::uint32_t instruction_starts[]={'+','.join(str(pc)+'u' for pc in sorted(bodies))+'};\n'+
        'inline constexpr std::uint32_t recovered_callback_entries[]={'+','.join(str(pc)+'u' for pc in callbacks['recovered_entries'])+'};\n'+
        'inline constexpr std::uint32_t recovered_jump_entries[]={'+','.join(str(pc)+'u' for pc in callbacks['recovered_jump_entries'])+'};\n}\n')
    report = dict(schema='wa2.special-shared-aot.v1', input_sha256=receipt['input_sha256'],
        instruction_locations=len(bodies), function_candidates=len(graph['function_candidates']), static_bodies=len(merged),
        all_discovered_instructions_compiled=True, all_direct_edges_resolve=True,
        recovered_callback_entries=callbacks['recovered_entries'],pointer_slots_audited=callbacks['pointer_slots_audited'],
        recovered_jump_entries=callbacks['recovered_jump_entries'],shared_bulk_string_kernel=True,sampled_static_work=True,
        pending_transcendental_sites=pending,supported_transcendental_sites=transcendental,
        transcendental_verification_cases=math_proof['cases'],native_lzss_prefix=True,differential_probe_entries=probes,
        shared_vorbis_host_boundaries=codec_entries,
        measured_hot_entries=sorted(hot),shared_register_fp_and_integer_kernels=True,
        shared_sprite_and_glyph_entries=sorted(draw_entries),source_drawing_profile=drawing,
        shared_sort_prefix=sorting,shared_sort_owner=sort_owner,
        hot_optimization='O2',other_optimization='Oz',
        main_story_address_hooks_applied=False, source_executable_opened=False,
        game_launched=False, screen_access=False, whole_program_complete=False,
        generated_sha256={p.name: digest(p) for p in sorted(OUT.iterdir()) if p.is_file() and p.name != 'manifest.json'})
    write_json(OUT/'manifest.json', report)
    print(json.dumps({k: report[k] for k in ('instruction_locations','static_bodies','all_direct_edges_resolve')}), flush=True)


if __name__ == '__main__':
    generate()
