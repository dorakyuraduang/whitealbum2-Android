"""Prepare external ua.pak/font_ua.pak from the verified UA 0.1.5 ZIP.

Only resources are read. No installer, EXE or DLL is executed or distributed.
The mixed script/UI pack keeps main and IC scripts in distinct KCAP groups.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import struct
import subprocess
import tempfile
import zipfile

RELEASE_SHA='8b57eb7963a89280c61e44a125d7941c859e506220d6281469e5cf89d1b8cf96'
FONT_NAMES={b'\x96\x7b\x91\xcc80.tga',b'\x91\xdc\x89\x65' b'80.tga',b'14pt\x96\x7b\x91\xcc.tga',b'14pt\x91\xdc.tga'}

def index(path):
    with path.open('rb') as stream:
        size=path.stat().st_size;header=stream.read(16)
        if len(header)!=16 or header[:4]!=b'KCAP':raise ValueError('Expected KCAP patch archive')
        count=struct.unpack_from('<I',header,12)[0]
        if not 0<count<100000 or 16+count*44>size:raise ValueError('Invalid KCAP index')
        rows=stream.read(count*44);groups={};group=None
        for n in range(count):
            row=rows[n*44:(n+1)*44];flags=struct.unpack_from('<I',row)[0];name=row[4:28].split(b'\0',1)[0];offset,length=struct.unpack_from('<II',row,36)
            if not length:
                if name in groups:raise ValueError('Duplicate KCAP group')
                group=name;groups[group]=[];continue
            if group is None or flags>1 or offset<16+count*44 or length>size-offset:raise ValueError('Invalid KCAP member')
            if any(old[0].lower()==name.lower() for old in groups[group]):raise ValueError('Duplicate KCAP member')
            groups[group].append((name,flags,path,offset,length))
    if set(groups)!={b'bak',b'fnt',b'grp',b'script'}:raise ValueError('Unexpected patch group layout')
    return groups

def write_pak(path,groups):
    entries=[]
    for group,members in groups:
        if group is not None:entries.append((group,0,None,0,0))
        entries.extend(members)
    offset=16+44*len(entries);rows=bytearray(44*len(entries))
    for n,(name,flags,source,start,size) in enumerate(entries):
        if not name or len(name)>24 or offset+size>0xffffffff:raise ValueError('KCAP name or offset limit')
        struct.pack_into('<I',rows,n*44,flags);rows[n*44+4:n*44+4+len(name)]=name;struct.pack_into('<II',rows,n*44+36,offset,size);offset+=size
    with path.open('xb') as output:
        output.write(b'KCAP'+b'\0'*8+struct.pack('<I',len(entries)));output.write(rows)
        for name,flags,source,start,size in entries:
            if not size:continue
            with source.open('rb') as stream:
                stream.seek(start);remaining=size
                while remaining:
                    chunk=stream.read(min(remaining,1024*1024))
                    if not chunk:raise ValueError('Truncated resource while writing KCAP')
                    output.write(chunk);remaining-=len(chunk)
        output.flush();os.fsync(output.fileno())

def prepare(archive,output,encoder):
    archive=Path(archive).resolve();output=Path(output).resolve();encoder=Path(encoder).resolve()
    with archive.open('rb') as stream:
        if hashlib.file_digest(stream,'sha256').hexdigest()!=RELEASE_SHA:raise ValueError('ZIP differs from the verified UA 0.1.5 release')
    output.mkdir(parents=True,exist_ok=True)
    targets=[output/'ua.pak',output/'font_ua.pak']
    if any(p.exists() for p in targets):raise ValueError('ua.pak/font_ua.pak already exists; back it up before preparation')
    flags=(subprocess.CREATE_NO_WINDOW|subprocess.BELOW_NORMAL_PRIORITY_CLASS) if os.name=='nt' else 0
    with tempfile.TemporaryDirectory(prefix='.wa2-ua-',dir=output) as temporary,zipfile.ZipFile(archive) as z:
        temp=Path(temporary);manifest=json.loads(z.read('manifest.json'));names=set()
        for entry in z.infolist():
            path=PurePosixPath(entry.filename)
            if path.is_absolute() or '..' in path.parts or '\\' in entry.filename or ':' in entry.filename or entry.filename.casefold() in names or entry.flag_bits&1:raise ValueError('Unsafe ZIP member')
            names.add(entry.filename.casefold())
        if manifest['version']!='0.1.5' or len(manifest['files'])!=439:raise ValueError('Unexpected translation manifest')
        # Validate every member before publishing either output.
        for name,expected in manifest['files'].items():
            with z.open('payload/'+name) as stream:
                if hashlib.file_digest(stream,'sha256').hexdigest()!=expected.lower():raise ValueError('Patch checksum mismatch: '+name)
        base=temp/'base.pak'
        with z.open('payload/en.pak') as source,base.open('xb') as destination:shutil.copyfileobj(source,destination,1024*1024)
        groups=index(base)
        def loose(prefix):
            result=[]
            for name in manifest['files']:
                if not name.startswith(prefix):continue
                leaf=name[len(prefix):]
                if '/' in leaf or not leaf.endswith(('.txt','.bnr')):raise ValueError('Unexpected script path')
                target=temp/(prefix.replace('/','_')+leaf);target.write_bytes(z.read('payload/'+name));result.append((leaf.encode('ascii'),0,target,0,target.stat().st_size))
            leaves={row[0] for row in result}
            if any(name[:-4]+b'.txt' not in leaves for name in leaves if name.endswith(b'.bnr')) or len(result)%2:raise ValueError('Unpaired scene scripts')
            return result
        main=loose('script/');ic=loose('IC/script/')
        if len(main)!=372 or len(ic)!=60:raise ValueError('Unexpected main/IC script count')
        override={row[0].lower():row for row in main};merged=[override.pop(row[0].lower(),row) for row in groups[b'script']];merged.extend(override.values())
        fonts=[]
        for row in groups[b'fnt']:
            name=row[0]
            if name not in FONT_NAMES:raise ValueError('Unexpected font atlas')
            loose_name='fnt/'+name.decode('cp932')
            if loose_name in manifest['files']:
                raw=z.read('payload/'+loose_name)
                if len(raw)<18 or raw[2]!=2 or struct.unpack_from('<HH',raw,12)!=(3200,3840) or raw[16]!=32:raise ValueError('Unexpected Ukrainian font geometry')
                packed=subprocess.run([str(encoder)],input=raw,capture_output=True,creationflags=flags,timeout=600)
                if packed.returncode:raise ValueError('LZSS encoder failed: '+packed.stderr.decode('utf-8',errors='replace'))
                if len(packed.stdout)<8 or struct.unpack_from('<II',packed.stdout)!=(len(packed.stdout),len(raw)):raise ValueError('Invalid LZSS encoder result')
                target=temp/('font-'+name.hex());target.write_bytes(packed.stdout);row=(name,1,target,0,len(packed.stdout))
            fonts.append(row)
        if {row[0] for row in fonts}!=FONT_NAMES:raise ValueError('Four matched font atlases are required')
        prepared=[temp/'ua.pak',temp/'font_ua.pak']
        write_pak(prepared[0],[(b'bak',groups[b'bak']),(b'grp',groups[b'grp']),(b'script',merged),(b'ic/script',ic)])
        write_pak(prepared[1],[(None,fonts)])
        created=[]
        try:
            for source,target in zip(prepared,targets):
                with target.open('xb') as destination:
                    created.append(target)
                    with source.open('rb') as stream:shutil.copyfileobj(stream,destination,1024*1024)
                    destination.flush();os.fsync(destination.fileno())
        except BaseException:
            for target in created:target.unlink()
            raise
    return dict(files=[dict(path=str(p),bytes=p.stat().st_size) for p in targets],main_scene_pairs=186,ic_scene_pairs=30,source_modified=False,executables_included=False)

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--zip',type=Path,required=True);parser.add_argument('--out',type=Path,required=True);parser.add_argument('--encoder',type=Path,required=True)
    args=parser.parse_args();print(json.dumps(prepare(args.zip,args.out,args.encoder),ensure_ascii=False))
