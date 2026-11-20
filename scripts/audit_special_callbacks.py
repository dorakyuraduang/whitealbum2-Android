"""Close Special's initialized callback references using its SHA-bound cache.

Never opens an original EXE or runs game code. Independent static listing/data
types distinguish pointer tables from text; the existing frontend proves every
new instruction and direct edge before the shared AOT generator can use it.
"""
from __future__ import annotations
import bisect
import json
import math
import os
from pathlib import Path
import struct
import subprocess

from prepare_special_contents import ROOT, candidates, digest, write_json, capstone, pefile

CACHE=ROOT/'generated/special-contents'
OUT=CACHE/'callback-upgrade'
WORK=ROOT/'build/special-reference-analysis'
TOOL=ROOT/'build/special-toolchain/winrecomp.exe'
EXPORT=ROOT/'scripts/analysis/ExportCachedListing.java'
MANIFEST=OUT/'manifest.json'


def checked_base():
    receipt=json.loads((CACHE/'preparation.json').read_text())
    profile=json.loads((ROOT/'config/special_contents.json').read_text())
    if receipt['input_sha256']!=profile['sha256']:
        raise ValueError('Special callback input identity changed')
    for name,sha in receipt['cache_sha256'].items():
        if digest(ROOT/name)!=sha:raise ValueError('Special base cache changed: '+name)
    return receipt


def reference_listing():
    image=CACHE/'image.bin';identity=digest(image)
    receipt_path=WORK/'export-receipt.json';listing=WORK/'listing.json'
    if receipt_path.exists() and listing.exists():
        receipt=json.loads(receipt_path.read_text())
        if (receipt['input_sha256']==identity and receipt['exporter_sha256']==digest(EXPORT)
                and receipt['listing_sha256']==digest(listing)):
            return listing
    for name in ('projects','settings','cache','temp'):(WORK/name).mkdir(parents=True,exist_ok=True)
    ghidra=ROOT/'tools/source-analysis/ghidra_12.1.4_PUBLIC'
    jdk=ROOT/'tools/source-analysis/jdk-21.0.12.1+1'
    project='special-references-'+identity[:12]
    existing=(WORK/'projects'/(project+'.gpr')).exists()
    env=os.environ.copy();env.update(JAVA_HOME=str(jdk),GHIDRA_HEADLESS_MAXMEM='1536M',
        GHIDRA_HEADLESS_JAVA_OPTIONS=' '.join(['-XX:ActiveProcessorCount=1','-XX:+UseSerialGC',
            '-XX:ParallelGCThreads=1','-Djava.awt.headless=true',
            '-Dapplication.settingsdir='+str(WORK/'settings'),'-Dapplication.cachedir='+str(WORK/'cache'),
            '-Dapplication.tempdir='+str(WORK/'temp'),'-Djava.io.tmpdir='+str(WORK/'temp')]),
        TEMP=str(WORK/'temp'),TMP=str(WORK/'temp'))
    command=['cmd.exe','/d','/c',str(ghidra/'support/analyzeHeadless.bat'),str(WORK/'projects'),project]
    command+=['-process','image.bin','-noanalysis'] if existing else ['-import',str(image),'-analysisTimeoutPerFile','180']
    command+=['-max-cpu','1','-scriptPath',str(ROOT/'scripts/analysis'),'-postScript',EXPORT.name,str(listing)]
    with (ROOT/'logs/special-reference-export.log').open('wb') as log:
        subprocess.run(command,cwd=ROOT,env=env,creationflags=subprocess.CREATE_NO_WINDOW|subprocess.BELOW_NORMAL_PRIORITY_CLASS,
            stdout=log,stderr=subprocess.STDOUT,check=True,timeout=600)
    data=json.loads(listing.read_text())
    if data.get('input_sha256')!=identity or 'static_code_pointer_slots' not in data:
        raise ValueError('Independent Special reference listing has the wrong identity or schema')
    write_json(receipt_path,dict(input_sha256=identity,exporter_sha256=digest(EXPORT),
        listing_sha256=digest(listing),new_cached_image_analysis=not existing,
        original_source_file_opened=False,game_launched=False,screen_access=False))
    return listing


def validated_cache():
    manifest=json.loads(MANIFEST.read_text())
    if not manifest['passed'] or manifest['input_sha256']!=json.loads((ROOT/'config/special_contents.json').read_text())['sha256']:
        raise ValueError('Special callback audit is missing or belongs to another input')
    for name,sha in {**manifest['source_sha256'],**manifest['cache_sha256']}.items():
        if digest(ROOT/name)!=sha:raise ValueError('Stale Special callback audit: '+name)
    return manifest


def audit():
    base=checked_base()
    if MANIFEST.exists():
        try:
            proof=validated_cache();print('Reusing verified Special callback closure.',flush=True);return proof
        except ValueError:
            pass
    listing_path=reference_listing();listing=json.loads(listing_path.read_text())
    if listing['input_sha256']!=base['input_sha256']:raise ValueError('Special static listing identity mismatch')
    data=(CACHE/'image.bin').read_bytes();pe=pefile.PE(data=data)
    original=json.loads((CACHE/'cfg.json').read_text());metadata=original['image'];image_base=metadata['image_base']
    def file_offset(address,size=4):
        for section in metadata['sections']:
            start=image_base+section['rva']
            if start<=address and address+size<=start+section['raw_size']:
                return address-start+section['raw_offset']
    def executable(address):
        return any(s['characteristics']&0x20000000 and image_base+s['rva']<=address<image_base+s['rva']+min(s['raw_size'],s['virtual_size']) for s in metadata['sections'])
    independent={i['address']:i for i in listing['instructions']}
    function_refs={(ref['from'],function['address']) for function in listing['functions']
                   for ref in function['references'] if ref['type']=='DATA'}
    def short_text_record(address):
        # Locale tables contain repeated {three ASCII letters, data pointer}
        # records. Their packed keys can look exactly like .text addresses;
        # even an automatic pointer type is insufficient evidence of a call.
        offset=file_offset(address,8)
        if offset is None:return False
        text=data[offset:offset+4]
        if text[3]!=0 or not all(65<=value<=90 for value in text[:3]):return False
        value=struct.unpack_from('<I',data,offset+4)[0]
        return file_offset(value,1) is not None and not executable(value)
    def text_table_key(address):
        return short_text_record(address) and any(short_text_record(address+delta) for delta in (-8,8))
    def numeric_table_word(address):
        # Low halves of smooth double tables can also spell code addresses.
        # Only untyped, unreferenced fields are considered here, never typed
        # pointer arrays or callback references attached to a known function.
        if address%8:return False
        offset=file_offset(address-8,24)
        if offset is None:return False
        values=struct.unpack_from('<ddd',data,offset)
        if not all(math.isfinite(v) and 2**-64<abs(v)<2**64 for v in values):return False
        return values[0]<=values[1]<=values[2] or values[0]>=values[1]>=values[2]
    slots=listing['static_code_pointer_slots'];selected={};excluded=[]
    known={i['address']:i['size'] for i in original['instructions']};ordered=sorted(known)
    decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
    for row in slots:
        source,target=row['from'],row['to'];offset=file_offset(source)
        if offset is None or struct.unpack_from('<I',data,offset)[0]!=target or not executable(target):
            raise ValueError('Independent pointer reference differs from cached initial memory')
        if target in known:continue
        previous=bisect.bisect_right(ordered,target)-1
        reason=None
        if row['source_is_string']:reason='typed_text_constant'
        elif text_table_key(source):reason='short_text_key_with_data_pointer'
        elif row['source_type']=='undefined' and (source,target) not in function_refs and numeric_table_word(source):reason='numeric_table_word'
        elif row['target_interior'] or previous>=0 and ordered[previous]+known[ordered[previous]]>target:
            reason='interior_of_existing_instruction'
        else:
            at=file_offset(target,1);first=next(decoder.disasm(data[at:at+16],target,count=1),None)
            if not first or first.mnemonic in ('int3','hlt','ud2'):reason='padding_or_invalid_entry'
        if reason:
            if row['source_is_pointer'] and reason not in ('padding_or_invalid_entry','short_text_key_with_data_pointer'):
                raise ValueError(f'Typed callback conflicts with an instruction boundary at {target:#x}')
            excluded.append(dict(source=source,target=target,reason=reason));continue
        selected.setdefault(target,[]).append(dict(source=source,source_type=row['source_type'],
            independent_boundary=row['target_instruction'],typed_pointer=row['source_is_pointer']))
    if 4739667 not in selected:raise ValueError('Reported CRT initializer was not independently recovered')
    OUT.mkdir(parents=True,exist_ok=True)
    flags=subprocess.CREATE_NO_WINDOW|subprocess.BELOW_NORMAL_PRIORITY_CLASS
    seeds=set(base['seeds'])|set(selected);graph=original;history=[];jump_edges=set()
    def run(mode,destination):
        command=[str(TOOL),mode,str(CACHE/'image.bin'),str(destination)]
        for entry in sorted(seeds):command+=['--seed',hex(entry)]
        result=subprocess.run(command,cwd=ROOT,creationflags=flags,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=300)
        with (ROOT/'logs/special-callback-discovery.log').open('ab') as log:log.write(result.stdout)
        if result.returncode:raise RuntimeError(f'Special callback {mode} failed: '+result.stdout.decode('utf-8',errors='replace')[-2000:])
    for round_number in range(8):
        run('cfg',OUT/'cfg.json');graph=json.loads((OUT/'cfg.json').read_text())
        if graph['diagnostics']:raise ValueError('Special callback discovery diagnostics: '+repr(graph['diagnostics'][:20]))
        live={i['address'] for i in graph['instructions']};new=candidates(pe,graph,seeds)
        for instruction in graph['instructions']:
            pc=instruction['address'];record=independent.get(pc)
            if record is None:continue
            for target in record.get('code_references',[]):
                if target not in live and target not in seeds and target in independent and executable(target):
                    new[target]=f'independent_code_reference:0x{pc:x}'
            # MSVC embeds memcpy/memmove switch tables inside .text, outside
            # the initialized data-pointer scan. Consume the independently
            # resolved computed edges, never the table words as instructions.
            if 'COMPUTED' in record['flow_type'] and 'JUMP' in record['flow_type']:
                for target in record['flows']:
                    if not executable(target):continue
                    if record['bytes']!=instruction['bytes'] or target not in independent:
                        raise ValueError(f'Independent switch boundary differs at {pc:#x}')
                    jump_edges.add((pc,target))
                    if target not in live and target not in seeds:
                        new[target]=f'independent_computed_jump:0x{pc:x}'
        fresh=set(new)-seeds
        history.append(dict(round=round_number,instructions=len(live),seeds=len(seeds),new_seeds=len(fresh)))
        print(json.dumps(history[-1]),flush=True)
        if not fresh:break
        if round_number==7 or len(seeds|fresh)>1500:raise ValueError('Special callback closure did not converge')
        seeds.update(fresh)
    live={i['address'] for i in graph['instructions']}
    if not set(selected)<=live:raise ValueError('A recovered Special callback is still untranslated')
    if any(target not in live for _,target in jump_edges):
        raise ValueError('An independently resolved Special jump target is still untranslated')
    recovered_jumps=sorted({target for _,target in jump_edges if target not in known})
    if 4673248 not in recovered_jumps:raise ValueError('Reported memcpy switch branch was not recovered')
    run('coverage',OUT/'coverage.json')
    if json.loads((OUT/'coverage.json').read_text())['unsupported']:
        raise ValueError('A Special callback uses an unsupported instruction form')
    raw=OUT/('raw-'+digest(OUT/'cfg.json')[:16])
    if not raw.exists():run('project',raw)
    sources=[Path(__file__),EXPORT,CACHE/'preparation.json',CACHE/'image.bin',listing_path,TOOL]
    outputs=[OUT/'cfg.json',OUT/'coverage.json',*sorted(raw.glob('code_page_*.cpp'))]
    proof=dict(schema='wa2.special-callback-audit.v1',input_sha256=base['input_sha256'],
        base_instruction_locations=len(known),instruction_locations=len(live),
        pointer_slots_audited=len(slots),recovered_entries=sorted(selected),
        recovered_jump_entries=recovered_jumps,independent_jump_edges=sorted(jump_edges),
        all_independently_resolved_jump_targets_compiled=True,
        raw_directory=raw.relative_to(ROOT).as_posix(),
        pointer_references={hex(k):v for k,v in sorted(selected.items())},excluded_candidates=excluded,
        discovery_history=history,seeds=sorted(seeds),all_recovered_entries_resolve=True,
        source_sha256={p.relative_to(ROOT).as_posix():digest(p) for p in sources},
        cache_sha256={p.relative_to(ROOT).as_posix():digest(p) for p in outputs},
        original_source_file_opened=False,game_launched=False,screen_access=False,whole_program_complete=False,passed=True)
    write_json(MANIFEST,proof)
    print('Recovered Special callback entries:',len(selected),flush=True)
    return proof


if __name__=='__main__':audit()
