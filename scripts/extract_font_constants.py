"""Recover typed font constants from an existing compilation cache, never the source EXE."""
import hashlib
import json
from pathlib import Path
import struct

ROOT=Path(__file__).resolve().parents[1]
CACHED=ROOT/'build/wa2-win64/CMakeFiles/recompiled_program.dir/wa2_port/embedded_image.S.obj'


def digest(data):return hashlib.sha256(data).hexdigest()


def main():
    coff=CACHED.read_bytes()
    machine,sections,_,_,_,optional,_=struct.unpack_from('<HHIIIHH',coff)
    if machine!=0x8664:raise ValueError('Unexpected cached object architecture')
    payload=None
    for i in range(sections):
        at=20+optional+i*40
        if coff[at:at+8].rstrip(b'\0')==b'.rdata':
            size,offset=struct.unpack_from('<II',coff,at+16)
            if offset+size>len(coff):raise ValueError('Truncated cached data section')
            payload=coff[offset:offset+size]
    if payload is None or digest(payload)!='246a263b965147a274fa59cdd7a72f6be10ba063d8cf4e61e74baefb56f29327':
        raise ValueError('Cached embedded-data provenance mismatch')
    pe=struct.unpack_from('<I',payload,0x3c)[0]
    if payload[pe:pe+4]!=b'PE\0\0':raise ValueError('Invalid cached data layout')
    count=struct.unpack_from('<H',payload,pe+6)[0];options=struct.unpack_from('<H',payload,pe+20)[0]
    base=struct.unpack_from('<I',payload,pe+24+28)[0]
    def constant(address,size):
        for i in range(count):
            at=pe+24+options+i*40;virtual,raw_size,raw=struct.unpack_from('<III',payload,at+12)
            flags=struct.unpack_from('<I',payload,at+36)[0]
            if virtual<=address-base and address-base+size<=virtual+raw_size:
                if flags&0x20000000:raise ValueError('Font constants must come from nonexecutable cached data')
                offset=raw+address-base-virtual;return payload[offset:offset+size]
        raise ValueError('Missing cached font constant')
    tables=[]
    for address in (0x4a1420,0x4a14b8):
        ranges=[]
        for i in range(128):
            first,last=struct.unpack('<HH',constant(address+i*4,4))
            if first==0xffff or last==0xffff:break
            if first>last:raise ValueError('Invalid existing glyph range')
            ranges.append((first,last))
        else:raise ValueError('Missing font-table terminator')
        tables.append(ranges)
    generated=ROOT/'engine/font_ranges.inc'
    content='// Typed constants from the existing compilation cache; see logs/native-font-constants.json.\n'
    for name,ranges in zip(('base_ranges','extended_ranges'),tables):
        content+='static constexpr std::array<CodeRange,'+str(len(ranges))+'> '+name+'{{\n'
        content+=''.join('{0x%04x,0x%04x},\n'%pair for pair in ranges)+'}};\n'
    generated.write_text(content)
    directory=ROOT/'build/native-fonts';directory.mkdir(parents=True,exist_ok=True)
    fixture=directory/'font-ranges.bin'
    fixture.write_bytes(b'W2FR'+struct.pack('<HH',*map(len,tables))+b''.join(struct.pack('<HH',*pair) for table in tables for pair in table))
    proof=dict(schema='wa2.native-font-constants.v1',cached_object=str(CACHED),cached_object_sha256=digest(coff),
        generated_source=str(generated),generated_source_sha256=digest(generated.read_bytes()),
        fixture=str(fixture),fixture_sha256=digest(fixture.read_bytes()),
        range_counts=list(map(len,tables)),original_executable_opened=False,cached_embedded_image_data_read=True,
        new_instruction_analysis=False,executable_code_disassembled=False,screen_access=False)
    (ROOT/'logs/native-font-constants.json').write_text(json.dumps(proof,indent=2)+'\n')
    print('Recovered',sum(map(len,tables)),'typed glyph ranges from the existing cached data object; no source EXE opened.')


if __name__=='__main__':main()
