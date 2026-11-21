"""Prepare the SHA-bound English SC data variant; never decode or run code.

The original Main executable is never opened. All hosts keep the existing SC
static functions; only the image headers, read-only text and PE resources vary.
Private input bytes and external game resources must not enter public source.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
import zipfile

ROOT=Path(__file__).resolve().parents[1]
CN='1a93d3126a90c68d07c0f79ec19e3b282b3084538e1f92c3456525ba93103ae1'
EN='849fd5e414a0ee2b34cec65aba4981c6101f9a59d57b6299c832ae77080b9736'
CACHE=ROOT/'generated/special-contents/english'
def sha(data):return hashlib.sha256(data).hexdigest()
def pe(data):
    # Data-only PE metadata parser. No instruction discovery or execution.
    if data[:2]!=b'MZ':raise ValueError('Expected PE data')
    at=struct.unpack_from('<I',data,60)[0]
    if data[at:at+6]!=b'PE\0\0L\x01':raise ValueError('Expected I386 PE')
    count=struct.unpack_from('<H',data,at+6)[0];optional=at+24
    size=struct.unpack_from('<H',data,at+20)[0]
    if struct.unpack_from('<H',data,optional)[0]!=267 or count!=4:raise ValueError('Unsupported SC PE layout')
    sections=[]
    for n in range(count):
        p=optional+size+40*n;name=data[p:p+8].rstrip(b'\0').decode('ascii')
        virtual_size,rva,raw_size,raw_offset=struct.unpack_from('<4I',data,p+8)
        sections.append(dict(name=name,rva=rva,virtual_size=virtual_size,raw_size=raw_size,raw_offset=raw_offset,characteristics=struct.unpack_from('<I',data,p+36)[0]))
    return dict(image_base=struct.unpack_from('<I',data,optional+28)[0],entry=struct.unpack_from('<I',data,optional+16)[0],image_size=struct.unpack_from('<I',data,optional+56)[0],headers_size=struct.unpack_from('<I',data,optional+60)[0],sections=sections,directories=[dict(rva=a,size=b) for a,b in struct.iter_unpack('<II',data[optional+96:optional+224])])

def prepare(archive):
    original=(ROOT/'generated/special-contents/image.bin').read_bytes()
    if sha(original)!=CN:raise ValueError('Wrong cached SC source identity')
    with zipfile.ZipFile(archive) as z:
        item=z.getinfo('WA2_Special_en.exe')
        if item.file_size!=943104:raise ValueError('Unsupported English executable size')
        english=z.read(item)
    if sha(english)!=EN:raise ValueError('Unsupported English executable SHA-256')
    a,b=pe(original),pe(english)
    if (a['image_base'],a['entry'])!=(b['image_base'],b['entry']):raise ValueError('English code identity differs')
    patches=[];section_proof={}
    for left,right in zip(a['sections'],b['sections']):
        if left['name']!=right['name']:raise ValueError('Different section order')
        x=original[left['raw_offset']:left['raw_offset']+left['raw_size']]
        y=english[right['raw_offset']:right['raw_offset']+right['raw_size']]
        section_proof[left['name']]=dict(cached_sha256=sha(x),english_sha256=sha(y),identical=x==y)
        if left['name'] in ('.text','.data'):
            if left!=right or x!=y:raise ValueError('English code or initialized state differs; a data-only variant is unsafe')
        elif left['name'] not in ('.rdata','.rsrc'):raise ValueError('Unsupported English section')
    # Exact raw byte deltas are private generated data, never public patch text.
    at=0
    while at<len(english):
        if at<len(original) and original[at]==english[at]:at+=1;continue
        start=at
        while at<len(english) and (at>=len(original) or original[at]!=english[at]):at+=1
        patches.append(dict(offset=start,before=original[start:min(at,len(original))].hex(),after=english[start:at].hex()))
    reconstructed=bytearray(original);reconstructed.extend(b'\0'*max(0,len(english)-len(original)))
    for patch in patches:
        p=patch['offset'];raw=bytes.fromhex(patch['after']);reconstructed[p:p+len(raw)]=raw
    reconstructed=reconstructed[:len(english)]
    if bytes(reconstructed)!=english:raise ValueError('English data reconstruction differs')
    CACHE.mkdir(parents=True,exist_ok=True)
    report=dict(schema='wa2.special-english-data.v1',cached_sha256=CN,input_sha256=EN,size=len(english),metadata=b,sections=section_proof,patches=patches,shared_static_functions=True,source_executed=False)
    (CACHE/'preparation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    (CACHE/'image.bin').write_bytes(english)
    print(json.dumps(dict(english_sha256=EN,shared_text=True,shared_data=True,data_ranges=len(patches))))

def generate(write):
    proof=json.loads((CACHE/'preparation.json').read_text())
    original=(ROOT/'generated/special-contents/image.bin').read_bytes()
    if sha(original)!=CN or proof['input_sha256']!=EN:raise ValueError('English data receipt identity differs')
    image=bytearray(original);image.extend(b'\0'*max(0,proof['size']-len(image)))
    cpp=['#include "bodies.hpp"','#include <algorithm>','#include <stdexcept>','namespace wa2::special {','wr::Image english_image(){auto image=initial_image();',f'if(image.static_input_identity!="{CN}")throw std::runtime_error("SC cached image identity differs");',f'image.bytes.resize({proof["size"]}u);']
    for patch in proof['patches']:
        at=patch['offset'];old=bytes.fromhex(patch['before']);raw=bytes.fromhex(patch['after'])
        if original[at:at+len(old)]!=old:raise ValueError('Stale English data range')
        image[at:at+len(raw)]=raw
        cpp.append('{const std::uint8_t data[]={'+','.join(map(str,raw))+'};std::copy(std::begin(data),std::end(data),image.bytes.begin()+'+str(at)+'u);}')
    if sha(image)!=EN:raise ValueError('English generated image hash differs')
    cpp += [f'image.static_input_identity="{EN}";',f'image.size={proof["metadata"]["image_size"]}u;','image.sections.clear();']
    for s in proof['metadata']['sections']:
        cpp.append('image.sections.push_back({'+json.dumps(s['name'])+','+','.join(str(s[k])+'u' for k in ('rva','virtual_size','raw_offset','raw_size','characteristics'))+'});')
    for n,d in enumerate(proof['metadata']['directories']):cpp.append(f'image.directories[{n}]={{{d["rva"]}u,{d["size"]}u}};')
    cpp += ['return image;}','}']
    write('english_image.cpp','\n'.join(cpp)+'\n')

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('patch',type=Path);args=parser.parse_args();prepare(args.patch)
