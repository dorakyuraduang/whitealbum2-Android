"""Copy the English patch's packed fnt members into external fon_en.pak.

No payload is decompressed and no game resource is added to the application.
The source en.pak remains unchanged. Run against your own game directory.
"""
import argparse
import os
from pathlib import Path
import struct
import tempfile

REQUIRED={b'\x96\x7b\x91\xcc80.tga',b'\x91\xdc\x89\x65' b'80.tga',b'14pt\x96\x7b\x91\xcc.tga',b'14pt\x91\xdc.tga'}

def prepare(root):
    root=Path(root).resolve();source=root/'en.pak';target=root/'fon_en.pak'
    if target.exists():raise ValueError('fon_en.pak already exists; preserve it or move it aside before generating another font pack')
    with source.open('rb') as stream:
        size=os.fstat(stream.fileno()).st_size;header=stream.read(16)
        if len(header)!=16 or header[:4]!=b'KCAP':raise ValueError('en.pak must be a KCAP archive')
        count=struct.unpack_from('<I',header,12)[0]
        if not 0<count<=100000 or 16+count*44>size:raise ValueError('Invalid English PAK index')
        index=stream.read(count*44);members=[];group=b'';seen=False
        for n in range(count):
            row=index[n*44:(n+1)*44];flags=struct.unpack_from('<I',row)[0]
            name=row[4:28].split(b'\0',1)[0];offset,length=struct.unpack_from('<II',row,36)
            if not length:
                group=name
                if name==b'fnt':
                    if seen:raise ValueError('Duplicate fnt directory')
                    seen=True
                continue
            if group!=b'fnt':continue
            if flags>1 or offset<16+count*44 or offset+length>size:raise ValueError('Invalid packed English font member')
            if name in {m[0] for m in members}:raise ValueError('Duplicate English font member')
            members.append((name,flags,offset,length))
        if not REQUIRED.issubset({m[0] for m in members}):raise ValueError('English fnt directory lacks one of the four required game atlases')
        members.sort(key=lambda m:m[0].lower())
        output_header=header[:12]+struct.pack('<I',len(members));output_index=bytearray(len(members)*44)
        next_offset=16+len(output_index)
        for n,(name,flags,offset,length) in enumerate(members):
            if next_offset+length>0xffffffff:raise ValueError('Font pack exceeds KCAP offsets')
            struct.pack_into('<I',output_index,n*44,flags);output_index[n*44+4:n*44+4+len(name)]=name
            struct.pack_into('<II',output_index,n*44+36,next_offset,length);next_offset+=length
        temporary=None
        try:
            with tempfile.NamedTemporaryFile(prefix='.wa2-font-',suffix='.tmp',dir=root,delete=False) as output:
                temporary=Path(output.name);output.write(output_header);output.write(output_index)
                for name,flags,offset,length in members:
                    stream.seek(offset)
                    while length:
                        chunk=stream.read(min(length,1024*1024))
                        if not chunk:raise ValueError('Truncated English font payload')
                        output.write(chunk);length-=len(chunk)
                output.flush();os.fsync(output.fileno())
            # Exclusive publication: an existing player font is never replaced.
            with target.open('xb') as output,temporary.open('rb') as packed:
                try:
                    while chunk:=packed.read(1024*1024):output.write(chunk)
                except BaseException:
                    output.close();target.unlink();raise
        finally:
            if temporary is not None:temporary.unlink(missing_ok=True)
    return dict(file=str(target),members=len(members),bytes=target.stat().st_size,payloads_decompressed=False,source_modified=False)

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--root',type=Path,required=True)
    print(prepare(parser.parse_args().root))
