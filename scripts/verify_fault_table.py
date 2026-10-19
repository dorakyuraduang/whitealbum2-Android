"""Validate every compact recovery address against the compiled native object."""
from pathlib import Path
import hashlib
import json
import struct
import sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/python'))
import capstone

def verify():
    path=ROOT/'build/wa2-win64/wa2_port/CMakeFiles/wa2_port.dir/native_linear.S.obj'
    data=path.read_bytes();machine,count,_,symbols,number,optional,_=struct.unpack_from('<HHIIIHH',data)
    assert machine==0x8664 and optional==0
    sections=[]
    for n in range(count):
        at=20+n*40;size,offset=struct.unpack_from('<II',data,at+16);sections.append(data[offset:offset+size])
    strings=data[symbols+number*18:];names={};n=0
    while n<number:
        at=symbols+n*18;raw,value,section,_,_,aux=struct.unpack_from('<8sIhHBB',data,at)
        if raw[:4]==b'\0'*4:
            start=struct.unpack_from('<I',raw,4)[0];name=strings[start:strings.index(b'\0',start)].decode()
        else:name=raw.split(b'\0')[0].decode()
        names[name]=(section,value);n+=aux+1
    text_section,begin=names['wa2_native_linear_run'];end_section,end=names['wa2_native_linear_code_end'];assert text_section==end_section
    table_section,first=names['wa2_native_linear_faults'];last_section,last=names['wa2_native_linear_faults_end'];assert table_section==last_section
    text=sections[text_section-1][begin:end];table=sections[table_section-1][first:last];assert len(table)%6==0
    assert len(text)<=0x1000000
    manifest=json.loads((ROOT/'port/native_linear_manifest.json').read_text());assert len(table)==manifest['static_memory_fault_sites']*6
    decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64);previous=-1
    for index in range(0,len(table),6):
        at=int.from_bytes(table[index:index+3],'little');recovery=int.from_bytes(table[index+3:index+6],'little')
        assert previous<at<len(text) and recovery<len(text)
        assert next(decoder.disasm_lite(text[at:at+15],at,count=1),None)
        assert next(decoder.disasm_lite(text[recovery:recovery+15],recovery,count=1),None)
        previous=at
    return dict(schema='wa2.compact-fault-table.v1',object=str(path),object_sha256=hashlib.sha256(data).hexdigest(),
        spans=manifest['static_memory_fault_spans'],sites=manifest['static_memory_fault_sites'],entry_bytes=6,code_bytes=len(text),
        old_bytes=manifest['static_memory_fault_spans']*12,new_bytes=len(table),
        every_instruction_start_and_recovery_decodes=True,strictly_sorted=True,screen_access=False,passed=True)

if __name__=='__main__':
    result=verify();(ROOT/'logs/compact-fault-table-tests.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
