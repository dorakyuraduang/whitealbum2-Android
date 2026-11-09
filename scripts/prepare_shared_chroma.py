"""Derive two common AOT units from cached C++; never reads an executable.

The cached inputs stay byte-identical. Only six unmetered preheader hooks and
include paths differ in the derived units. Original bodies remain available
for declined layouts and the independent own-memory comparison fixture.
"""
import hashlib
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
HOOKS={
 'functions_1.cpp':{0x43884d:0x438889,0x438898:0x4388d5,0x438906:0x438971,0x438984:0x4389f1},
 'functions_2.cpp':{0x438a23:0x438a65,0x438a85:0x438af1},
}
def digest(data):return hashlib.sha256(data).hexdigest()
def write(path,data):
    path.parent.mkdir(parents=True,exist_ok=True)
    if not path.exists() or path.read_bytes()!=data:path.write_bytes(data)
def derive(data,hooks):
    text=data.decode('utf-8');newline='\r\n' if '\r\n' in text else '\n'
    text='#include "image_chroma.hpp"'+newline+text
    for old,new in (('#include "bodies.hpp"','#include "../bodies.hpp"'),('#include "../../','#include "../../../')):text=text.replace(old,new)
    for entry,exit in hooks.items():
        anchor=f'L_{entry:x}: {{{newline}s.eip=0x{entry:x}u;{newline}'
        if text.count(anchor)!=1:raise ValueError('Missing/ambiguous cached preheader: '+hex(entry))
        text=text.replace(anchor,anchor+f'if constexpr(!Metered)if(native_image_chroma(s,m))goto L_{exit:x};{newline}')
    return text.encode('utf-8')
def prepare():
    folder=ROOT/'generated/shared-aot';output=folder/'native';inputs={};outputs={};bodies=[]
    for name,hooks in HOOKS.items():
        data=(folder/name).read_bytes();inputs[(folder/name).relative_to(ROOT).as_posix()]=digest(data)
        derived=derive(data,hooks);path=output/name;write(path,derived);outputs[path.relative_to(ROOT).as_posix()]=digest(derived)
        entry='438820' if name=='functions_1.cpp' else '438a00';text=data.decode('utf-8');begin=text.index('template<bool Metered> void f_'+entry+'(')
        marker=f'template void f_{entry}<false>(Context&,wr::Memory&,Scope&,std::uint64_t&,std::uint32_t);';end=text.index(marker,begin)+len(marker);bodies.append(text[begin:end])
    reference='#include "functions.hpp"\nnamespace wa2::aot::chroma_reference {\n'+'\n'.join(bodies)+'''
void invoke(wr::Cpu& cpu,wr::Memory& memory){auto scope=memory.execution_scope();std::uint64_t unused{};const auto location=locate(cpu.eip);
if(cpu.eip>=0x438820 && cpu.eip<0x438a00)f_438820<false>(cpu,memory,scope,unused,location.resume);
else if(cpu.eip>=0x438a00 && cpu.eip<0x438b00)f_438a00<false>(cpu,memory,scope,unused,location.resume);
else throw std::logic_error("Unknown chroma reference PC");}
void step(wr::Cpu& cpu,wr::Memory& memory,std::uint64_t budget){auto scope=memory.execution_scope();const auto location=locate(cpu.eip);
try{if(cpu.eip>=0x438820 && cpu.eip<0x438a00)f_438820<true>(cpu,memory,scope,budget,location.resume);
else if(cpu.eip>=0x438a00 && cpu.eip<0x438b00)f_438a00<true>(cpu,memory,scope,budget,location.resume);
else throw std::logic_error("Unknown chroma reference PC");}
catch(const wr::GuestFault& error){if(error.kind==wr::FaultKind::budget && !budget)return;throw;}
throw std::logic_error("Chroma reference finished before the requested boundary");}
}
'''
    reference_path=ROOT/'build/chroma-reference/reference.cpp';write(reference_path,reference.encode('utf-8'))
    inputs[Path(__file__).relative_to(ROOT).as_posix()]=digest(Path(__file__).read_bytes())
    metadata=dict(schema='wa2.derived-chroma.v1',inputs=inputs,outputs=outputs,reference_file=reference_path.relative_to(ROOT).as_posix(),reference_sha256=digest(reference.encode('utf-8')),hooks=sum(map(len,HOOKS.values())),cached_inputs_unchanged=True,source_executable_opened=False)
    write(output/'provenance.json',(json.dumps(metadata,indent=2)+'\n').encode());return metadata
if __name__=='__main__':print(json.dumps(prepare(),indent=2))
