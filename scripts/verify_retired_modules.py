"""Check a retired library's original static boundaries and release inventories.

This is a build-time audit of this SHA-bound, unmodified executable. It checks
direct CFG edges, decoded absolute references, and aligned compiler callback
tables; it does not infer safety from a short gameplay trace.
"""
from pathlib import Path
import hashlib
import json
import re
import sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/python'))
import capstone as cs
import pefile
from retired_modules import DOCUMENT,MANIFEST,RANGES,GATEWAYS,RELEASE_EXCLUSIONS,contains,address

def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def read(path):return json.loads(Path(path).read_text())

def verify():
    image=ROOT/'WA2_chs.exe';assert digest(image)==DOCUMENT['input_sha256']
    graph=read(ROOT/'logs/cfg.json');instructions={i['address']:i for i in graph['instructions']}
    gateway_ranges=tuple((f['start'],f['end']) for f in GATEWAYS)
    inbound=[]
    for edge in graph['edges']:
        if isinstance(edge.get('to'),int) and contains(edge['to']) and not contains(edge['from']):
            assert contains(edge['from'],gateway_ranges),edge
            inbound.append(edge)
    assert len(inbound)==56
    metadata=tuple((address(item['start']),address(item['end'])) for module in DOCUMENT['modules'] for item in module['callback_metadata'])
    decoder=cs.Cs(cs.CS_ARCH_X86,cs.CS_MODE_32);decoder.detail=True
    decoded_inbound=[];external_metadata=[]
    for pc,item in instructions.items():
        if contains(pc):continue
        decoded=list(decoder.disasm(bytes.fromhex(item['bytes']),pc));assert len(decoded)==1
        for operand in decoded[0].operands:
            if operand.type==cs.x86.X86_OP_IMM:
                value=operand.imm
                if contains(value):
                    assert contains(pc,gateway_ranges),(hex(pc),decoded[0].op_str)
                    decoded_inbound.append((pc,value))
            elif operand.type==cs.x86.X86_OP_MEM:value=operand.mem.disp
            else:continue
            if contains(value,metadata):external_metadata.append(pc)
    assert len(decoded_inbound)==len(inbound) and not external_metadata
    pe=pefile.PE(str(image));callback_pointers=[]
    for section in pe.sections:
        if section.Name.rstrip(b'\0') not in (b'.rdata',b'.data'):continue
        data=section.get_data()
        for offset in range(0,len(data)-3,4):
            value=int.from_bytes(data[offset:offset+4],'little')
            if contains(value):
                location=0x400000+section.VirtualAddress+offset
                assert contains(location,metadata),(hex(location),hex(value))
                callback_pointers.append(dict(address=location,target=value))
    assert len(callback_pointers)==43
    assert not contains(0x400000+pe.OPTIONAL_HEADER.AddressOfEntryPoint)
    if hasattr(pe,'DIRECTORY_ENTRY_EXPORT'):
        assert not any(contains(0x400000+symbol.address) for symbol in pe.DIRECTORY_ENTRY_EXPORT.symbols if not symbol.forwarder)
    native=read(ROOT/'port/native_linear_manifest.json');compact=read(ROOT/'build/wa2-win64/compact-fallback/manifest.json')
    assert native['retired_module_manifest_sha256']==compact['retired_module_manifest_sha256']==digest(MANIFEST)
    expected={pc for pc in instructions if contains(pc,RELEASE_EXCLUSIONS)}
    assert set(compact['release_excluded_instruction_locations'])==expected
    assert compact['release_instructions']+len(expected)==compact['test_oracle_instructions']==139987
    actual_release=set();actual_oracle=set()
    for path in sorted((ROOT/'build/wa2-win64/compact-fallback').glob('code_page_*.cpp')):
        pcs=lambda text:{int(value,16) for value in re.findall(r'case 0x([0-9a-f]+)u:',text)}
        actual_oracle.update(pcs(path.read_text()))
        actual_release.update(pcs((path.parent/'release'/path.name).read_text()))
    assert actual_oracle==set(instructions) and actual_release==actual_oracle-expected
    assert not any(contains(pc,RELEASE_EXCLUSIONS) for pc in native['entry_points'])
    assert all(contains(pc) for pc in native['retired_module_instruction_locations'])
    assert not expected.intersection(set(native['entry_points']))
    fp=read(ROOT/'port/fp_sequences_manifest.json')
    assert fp['retired_module_manifest_sha256']==digest(MANIFEST)
    assert fp['generator_sha256']==digest(ROOT/'scripts/generate_fp_sequences.py') and fp['helpers_in_separate_comdats']
    assert fp['retired_sequences']==[row for row in fp['sequences'] if contains(row['entry'],RELEASE_EXCLUSIONS)]
    assert fp['release_sequences']==[row for row in fp['sequences'] if not contains(row['entry'],RELEASE_EXCLUSIONS)]
    sources=['scripts/retired_modules.py','scripts/verify_retired_modules.py','port/retired_modules.json',
             'scripts/compact_fallback.py','port/compact_fallback.cmake','scripts/generate_native_backend_v2.py',
             'port/native_vorbis.cpp','engine/vorbis_stream.cpp','engine/vorbis_stream.hpp','port/native_functions.json']
    sources+=['port/legacy_vorbis_kernels.hpp','port/legacy_vorbis_kernels.cpp','port/retired_codec_stub.cpp','port/fast_paths.cpp','port/CMakeLists.txt']
    sources+=['scripts/generate_fp_sequences.py','port/fp_sequences_release.cpp','port/fp_sequences.cpp']
    return dict(schema='wa2.retired-modules-verification.v1',original_sha256=digest(image),
        modules=DOCUMENT['modules'],direct_inbound_edges=inbound,decoded_inbound_edges=len(decoded_inbound),
        aligned_callback_pointers=callback_pointers,external_callback_table_references=0,
        release_cpp_instructions=compact['release_instructions'],oracle_cpp_instructions=compact['test_oracle_instructions'],
        removed_cpp_instructions=len(expected),retired_library_cpp_instructions=sum(contains(pc) for pc in instructions),
        removed_native_instructions=native['retired_module_instructions'],
        retired_fp_sequences=len(fp['retired_sequences']),release_fp_sequences=len(fp['release_sequences']),
        native_gateways=list(GATEWAYS),source_sha256={name:digest(ROOT/name) for name in sources},
        original_cfg_sha256=digest(ROOT/'logs/cfg.json'),release_cpp_only=True,test_oracle_complete=True,
        runtime_decoder=False,screen_access=False,passed=True)

if __name__=='__main__':
    report=verify();(ROOT/'logs/retired-modules-verification.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:report[k] for k in ('removed_cpp_instructions','removed_native_instructions','release_cpp_instructions','test_oracle_complete','passed')},indent=2))
