"""Factor identical compiled C++ fallback bodies; no runtime decoder or bytecode.

Original generated files remain the native generator's input. Every factored
body is expanded back and compared byte-for-byte before this generator writes
anything. Immediate operands are C++ function arguments at fixed call sites.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/python'))
import pefile
from retired_modules import RELEASE_EXCLUSIONS,contains as retired_contains,MANIFEST as RETIRED_MANIFEST
CASE=re.compile(r'case 0x([0-9a-f]+)u: \{ (.*?)\nreturn true; \}',re.S)
GUARD=re.compile(r'wr::check_code\(m,s.eip,\{([\d,]+)\}\);\n')
CONSTANT=re.compile(r'\b0x[0-9a-f]+u\b')

def write(path,data):
    if isinstance(data,str):data=data.encode('utf-8')
    if not path.exists() or path.read_bytes()!=data:path.write_bytes(data)

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--out',type=Path,required=True);a=ap.parse_args();out=a.out.resolve();out.mkdir(parents=True,exist_ok=True)
    raw=(ROOT/'WA2_chs.exe').read_bytes();sha=hashlib.sha256(raw).hexdigest()
    if sha!=json.loads((ROOT/'config/wa2.json').read_text())['sha256']:raise ValueError('Image/profile mismatch')
    image=pefile.PE(data=raw);code=image.get_memory_mapped_image()[:0xb0000].ljust(0xb0000,b'\0')
    groups={};pages=[];externs=set();count=0;release_count=0;excluded=[]
    for source in sorted((ROOT/'generated/wa2-win64').glob('code_page_*.cpp')):
        text=source.read_text();externs.update(re.findall(r'^extern "C"[^\n]+;',text,re.M));page=[];release_page=[]
        for pc,body in CASE.findall(text):
            address=int(pc,16);guard=GUARD.match(body)
            if not guard:raise ValueError('Missing instruction guard')
            expected=bytes(map(int,guard[1].split(',')))
            if code[address-0x400000:address-0x400000+len(expected)]!=expected:raise ValueError('Original code guard mismatch')
            semantics=body[guard.end():];arguments=[]
            def parameter(match):
                value=match[0]
                # Keep common masks and zero as compile-time constants.
                if value in ('0x0u','0x1u','0xffu','0xffffu','0xffffffffu'):return value
                arguments.append(value);return 'parameter_'+str(len(arguments)-1)
            normalized=CONSTANT.sub(parameter,semantics);expanded=normalized
            for n in reversed(range(len(arguments))):expanded=re.sub(r'\bparameter_'+str(n)+r'\b',arguments[n],expanded)
            if expanded!=semantics:raise ValueError('Factored body did not reproduce its source')
            key=(normalized,len(expected));name='wa2_compact_'+hashlib.sha256((str(len(expected))+'\n'+normalized).encode()).hexdigest()[:20]
            if key not in groups:groups[key]=(name,len(arguments))
            elif groups[key]!=(name,len(arguments)):raise ValueError('Factored function collision')
            args=''.join(','+value for value in arguments)
            statement=f'case 0x{pc}u: {name}(s,m,budget{args});return true;'
            page.append(statement);count+=1
            if retired_contains(address,RELEASE_EXCLUSIONS):excluded.append(address)
            else:release_page.append(statement);release_count+=1
        if not page:raise ValueError('Empty instruction page')
        page_name=source.stem
        def definition(statements):return '#include "compact_ops.hpp"\nbool '+page_name+'(wr::Cpu& s,wr::Memory& m,std::uint64_t& budget){switch(s.eip){\n'+'\n'.join(statements)+'\ndefault:return false;}}\n'
        pages.append((source.name,definition(page),definition(release_page)))
    section=next(s for s in image.sections if s.Characteristics&0x20000000)
    text_base=0x400000+section.VirtualAddress;text_offset=section.PointerToRawData;text_size=section.Misc_VirtualSize
    overrides={}
    for source,_,_ in pages:
        pc=int(Path(source).stem.split('_')[-1])*4096;offset=text_offset+pc-text_base
        expected=code[pc-0x400000:pc-0x400000+4096]
        if raw[offset:offset+4096]!=expected:overrides[pc]=expected
    header=['#pragma once','#include <winrecomp/compiled.hpp>','#include <winrecomp/sse.hpp>']
    definitions=['#include "compact_ops.hpp"','#include <winrecomp/compiled.hpp>',*sorted(externs),
        'extern "C" const std::uint8_t wa2_image_begin[];',
        'namespace { [[gnu::noinline]] void original_guard(wr::Memory& m,wr::U32 pc,unsigned size){',
        f'const auto offset=pc-{text_base}u;if(offset>={text_size}u || size>{text_size}u-offset || !m.matches_code(pc,std::span(wa2_image_begin+{text_offset}+offset,size)))throw wr::GuestFault(wr::FaultKind::unsupported,pc,"self-modified code differs from its static translation");','}}']
    for pc,data in overrides.items():definitions.append('static constexpr std::array<std::uint8_t,4096> page_'+str(pc)+'={'+','.join(map(str,data))+'};')
    definitions.append('namespace wr { bool wa2_compact_verify_page(Memory& m,U32 pc){')
    for pc in overrides:definitions.append(f'if(pc=={pc}u)return m.matches_code(pc,page_{pc});')
    definitions.extend([f'const auto offset=pc-{text_base}u;return offset<{text_size}u && offset+4096<={section.SizeOfRawData}u && m.matches_code(pc,std::span(wa2_image_begin+{text_offset}+offset,4096));','}}'])
    for (body,size),(name,argc) in groups.items():
        signature='void '+name+'(wr::Cpu& s,wr::Memory& m,std::uint64_t& budget'+''.join(',wr::U32 parameter_'+str(n) for n in range(argc))+')'
        header.append(signature+';');definitions.append(signature+'{original_guard(m,s.eip,'+str(size)+');\n'+body+'\n}')
    write(out/'original_code.bin',raw)
    write(out/'original_code.S','.section .rdata,"dr"\n.balign 16\n.globl wa2_image_begin\n.globl wa2_image_end\nwa2_image_begin:\n.incbin "'+(out/'original_code.bin').as_posix()+'"\nwa2_image_end:\n')
    write(out/'compact_ops.hpp','\n'.join(header)+'\n');write(out/'compact_ops.cpp','\n'.join(definitions)+'\n')
    (out/'release').mkdir(exist_ok=True)
    for name,text,release_text in pages:write(out/name,text);write(out/'release'/name,release_text)
    manifest=dict(schema='wa2.compact-fallback.v1',input_sha256=sha,instructions=count,shared_functions=len(groups),pages=len(pages),
        every_body_expanded_and_matched=True,original_byte_guards_preserved=True,shared_embedded_image=True,page_padding_overrides=len(overrides),runtime_instruction_decoder=False,runtime_bytecode=False,
        release_instructions=release_count,test_oracle_instructions=count,release_excluded_instruction_locations=excluded,
        release_excluded_instructions=len(excluded),retired_module_manifest_sha256=hashlib.sha256(RETIRED_MANIFEST.read_bytes()).hexdigest(),
        native_function_manifest_sha256=hashlib.sha256((ROOT/'port/native_functions.json').read_bytes()).hexdigest(),
        source_sha256={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted((ROOT/'generated/wa2-win64').glob('code_page_*.cpp'))})
    write(out/'manifest.json',json.dumps(manifest,indent=2)+'\n')
    print(json.dumps({k:v for k,v in manifest.items() if k not in ('source_sha256','release_excluded_instruction_locations')},indent=2))

if __name__=='__main__':main()
