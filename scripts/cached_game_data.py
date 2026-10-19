"""Read typed, nonexecutable constants from the existing compiled data object.

Never opens the original EXE and never decodes executable sections.
"""
import hashlib
from pathlib import Path
import struct

ROOT=Path(__file__).resolve().parents[1]
CACHED=ROOT/'build/wa2-win64/CMakeFiles/recompiled_program.dir/wa2_port/embedded_image.S.obj'


class Constants:
    def __init__(self):
        coff=CACHED.read_bytes()
        machine,count,_,_,_,optional,_=struct.unpack_from('<HHIIIHH',coff)
        if machine!=0x8664:raise ValueError('Unexpected cached object')
        self.object_sha256=hashlib.sha256(coff).hexdigest()
        payload=None
        for n in range(count):
            at=20+optional+n*40
            if coff[at:at+8].rstrip(b'\0')==b'.rdata':
                size,offset=struct.unpack_from('<II',coff,at+16)
                if offset+size>len(coff):raise ValueError('Truncated cached data')
                payload=coff[offset:offset+size]
        if payload is None or hashlib.sha256(payload).hexdigest()!='246a263b965147a274fa59cdd7a72f6be10ba063d8cf4e61e74baefb56f29327':
            raise ValueError('Cached data provenance mismatch')
        self.payload=payload
        pe=struct.unpack_from('<I',payload,0x3c)[0]
        if payload[pe:pe+4]!=b'PE\0\0':raise ValueError('Invalid cached data layout')
        count=struct.unpack_from('<H',payload,pe+6)[0];optional=struct.unpack_from('<H',payload,pe+20)[0]
        self.base=struct.unpack_from('<I',payload,pe+24+28)[0]
        self.sections=[]
        for n in range(count):
            at=pe+24+optional+n*40
            virtual,size,raw=struct.unpack_from('<III',payload,at+12);flags=struct.unpack_from('<I',payload,at+36)[0]
            self.sections.append((virtual,size,raw,flags))

    def read(self,address,size):
        for virtual,raw_size,raw,flags in self.sections:
            if virtual<=address-self.base and address-self.base+size<=virtual+raw_size:
                if flags&0x20000000:raise ValueError('Executable cached data is outside this reader\'s scope')
                offset=raw+address-self.base-virtual
                if offset+size>len(self.payload):raise ValueError('Truncated cached constant')
                return self.payload[offset:offset+size]
        raise ValueError('Missing cached constant: '+hex(address))

    def number(self,address,format='d'):
        return struct.unpack('<'+format,self.read(address,struct.calcsize('<'+format)))[0]
