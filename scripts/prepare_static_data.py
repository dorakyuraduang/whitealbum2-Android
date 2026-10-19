"""Preserve initial RAM from an EXISTING build object, without reading an EXE.

The cached CFG supplies section/import metadata. This is data copying from a
previous link input, not discovery, disassembly, or translation. The code bytes
are never executed by the host; the shared AOT supplies all executable bodies.
"""
import hashlib
import json
from pathlib import Path
import struct

ROOT=Path(__file__).resolve().parents[1]
OBJECT=ROOT/'build/wa2-win64/CMakeFiles/recompiled_program.dir/wa2_port/embedded_image.S.obj'
OUT=ROOT/'generated/static-data'


def write(path,value):
    if not path.exists() or path.read_text()!=value:path.write_text(value,encoding='utf-8',newline='\n')


def main():
    graph=json.loads((ROOT/'logs/cfg.json').read_text());metadata=graph['image']
    native=json.loads((ROOT/'port/native_functions.json').read_text())
    loops=json.loads((ROOT/'config/native_loops.json').read_text())['loops']
    identity=native['input_sha256']
    code,=[section for section in metadata['sections'] if section['characteristics']&0x20000000]
    code_base=metadata['image_base']+code['rva']
    # Only this exact prior build input is accepted. Do not fall back to an
    # executable, rebuild its .incbin rule, or scan the filesystem for binaries.
    with OBJECT.open('rb') as source:
        header=source.read(20)
        machine,count,_,symbols,symbol_count,optional,_=struct.unpack('<HHIIIHH',header)
        if machine!=0x8664 or count!=4 or optional:raise ValueError('unexpected cached object format')
        sections=[]
        for _ in range(count):
            fields=struct.unpack('<8sIIIIIIHHI',source.read(40));sections.append(fields)
        data_sections=[s for s in sections if s[0].rstrip(b'\0')==b'.rdata']
        if len(data_sections)!=1:raise ValueError('cached image storage is ambiguous')
        cached=data_sections[0]
        payload_size=max(s['raw_offset']+s['raw_size'] for s in metadata['sections'])
        if cached[3]!=payload_size or payload_size!=1220096:
            raise ValueError('cached data size differs from existing CFG metadata')
        # Validate the two assembler symbols, without parsing source headers.
        source.seek(symbols+symbol_count*18)
        string_size=struct.unpack('<I',source.read(4))[0]
        strings=b'\0\0\0\0'+source.read(string_size-4)
        source.seek(symbols);names={};index=0
        while index<symbol_count:
            raw=source.read(18);name,value,section,_,_,aux=struct.unpack('<8sIhHBB',raw)
            if name[:4]==b'\0'*4:
                offset=struct.unpack_from('<I',name,4)[0];name=strings[offset:].split(b'\0',1)[0]
            else:name=name.rstrip(b'\0')
            names[name]=(value,section)
            source.seek(aux*18,1);index+=aux+1
        section_index=sections.index(cached)+1
        if names.get(b'wa2_image_begin')!=(0,section_index) or names.get(b'wa2_image_end')!=(payload_size,section_index):
            raise ValueError('cached data assembler symbols disagree')
        OUT.mkdir(parents=True,exist_ok=True)
        chunks=[]
        # Keep the original data and code-address space intact, including data
        # tables embedded in .text. The cached bytes are initial RAM data;
        # they are never decoded or used as host-executable instructions.
        for name,offset,size in [('headers',0,min(s['raw_offset'] for s in metadata['sections']))]+[
                (s['name'].lstrip('.'),s['raw_offset'],s['raw_size']) for s in metadata['sections']]:
            source.seek(cached[4]+offset);value=source.read(size)
            if len(value)!=size:raise ValueError('truncated cached initial data')
            if name==code['name'].lstrip('.'):
                for instruction in graph['instructions']:
                    raw=bytes.fromhex(instruction['bytes']);at=instruction['address']-code_base
                    if at<0 or value[at:at+len(raw)]!=raw:raise ValueError('cached data and existing CFG instruction proof disagree')
            path=OUT/(name+'.inc')
            write(path,'\n'.join(','.join(str(b) for b in value[i:i+32])+',' for i in range(0,len(value),32))+'\n')
            chunks.append(dict(name=name,offset=offset,bytes=size,sha256=hashlib.sha256(value).hexdigest()))
    write(OUT/'static_data.hpp','#pragma once\n#include <cstdint>\n#include <span>\nnamespace wa2::aot {\n'+
          f'inline constexpr std::uint32_t static_code_base={code_base}u;\n'+
          f'extern const std::uint8_t static_code_bytes[{code["raw_size"]}];\n'+
          'struct NativeCodeContract {std::uint32_t entry,size;};\nstd::span<const NativeCodeContract> native_code_contracts() noexcept;\n}\n')
    cpp=['// Initial RAM from a prior build input. No source EXE read or parsed.',
         '#include <winrecomp/image.hpp>','#include <algorithm>','#include "static_data.hpp"','namespace wa2::aot {']
    for chunk in chunks:
        symbol='static_code_bytes' if chunk['name']==code['name'].lstrip('.') else chunk['name']
        cpp+=['const std::uint8_t '+symbol+'[]={','#include "'+chunk['name']+'.inc"','};']
    cpp+=['std::span<const NativeCodeContract> native_code_contracts() noexcept{','static constexpr NativeCodeContract contracts[]={']
    contracts={f['start']:f for f in native['functions']}
    closure_loops=[]
    for loop in loops:
        if loop.get('code_verification')=='shared_aot_direct_closure':
            # A multi-function batch has disjoint source spans. Program::prove
            # checks its entire immutable direct-call closure before the hook
            # can run; it must not be represented as one invented byte range.
            batch=json.loads((ROOT/'generated/shared-aot/weather_batch.json').read_text())
            if loop['implementation']!='recompiled/native_weather.cpp' or loop['entry'] not in batch['entries']:
                raise ValueError('Unrecognized native batch closure contract')
            closure_loops.append(loop['entry']);continue
        if loop['entry'] in contracts:raise ValueError('duplicate native code contract')
        contracts[loop['entry']]={'start':loop['entry'],'end':loop['end']}
    for function in sorted(contracts.values(),key=lambda f:f['start']):
        if not code_base<=function['start']<function['end']<=code_base+code['raw_size']:raise ValueError('native gateway range outside cached code')
        cpp.append(f'{{{function["start"]}u,{function["end"]-function["start"]}u}},')
    cpp+=['};return contracts;}','wr::Image initial_image(){','wr::Image image;',
          'image.static_input_identity='+json.dumps(identity)+';',
          f'image.base={metadata["image_base"]}u;image.entry={metadata["entry"]}u;image.size={metadata["image_size"]}u;',
          f'image.headers_size={chunks[0]["bytes"]}u;image.bytes.resize({payload_size}u);']
    for chunk in chunks:
        symbol='static_code_bytes' if chunk['name']==code['name'].lstrip('.') else chunk['name']
        cpp.append(f'std::copy(std::begin({symbol}),std::end({symbol}),image.bytes.begin()+{chunk["offset"]});')
    for section in metadata['sections']:
        cpp.append('image.sections.push_back({'+json.dumps(section['name'])+','+','.join(str(section[k])+'u' for k in ('rva','virtual_size','raw_offset','raw_size','characteristics'))+'});')
    for index,item in enumerate(metadata['directories']):cpp.append(f'image.directories[{index}]={{{item["rva"]}u,{item["size"]}u}};')
    for item in metadata['imports']:
        ordinal=str(item['ordinal']) if item['ordinal'] is not None else 'std::nullopt'
        cpp.append('image.imports.push_back({'+json.dumps(item['dll'])+','+json.dumps(item['name'])+','+str(item['iat'])+'u,'+ordinal+'});')
    for kind in ('exports','tls_callbacks'):
        for entry in metadata[kind]:cpp.append(f'image.{kind}.insert({entry}u);')
    cpp+=['return image;','}','}']
    write(OUT/'image.cpp','\n'.join(cpp)+'\n')
    report=dict(schema='wa2.cached-static-data.v1',cached_object=str(OBJECT.relative_to(ROOT)),
                metadata_source='logs/cfg.json',chunks=chunks,bytes=payload_size,
                known_instruction_locations_checked=len(graph['instructions']),native_gateway_contracts=len(native['functions']),
                native_loop_contracts=len(loops),
                native_loop_range_contracts=len(loops)-len(closure_loops),
                shared_aot_closure_loop_contracts=closure_loops,
                source_executable_opened=False,source_executable_parsed=False,
                instruction_discovery=False,guest_bytes_executed=False)
    write(OUT/'manifest.json',json.dumps(report,indent=2)+'\n')
    print(f'Preserved {payload_size} bytes of initial RAM from the existing build object only.')


if __name__=='__main__':main()
