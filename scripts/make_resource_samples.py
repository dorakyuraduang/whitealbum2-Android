"""Extract compressed members for byte-exact loading tests, without rendering."""
from pathlib import Path
import hashlib
import json
import struct

ROOT=Path(__file__).resolve().parents[1]

def decode(source,maximum,dictionary_fill=0):
    ring=bytearray([dictionary_fill])*0xfee+bytearray(18);position=0xfee;flags=0;at=0;output=bytearray()
    while at<len(source) and len(output)<maximum:
        flags>>=1
        if not flags&0x100:
            flags=source[at]|0xff00;at+=1
        if at>=len(source):break
        value=source[at];at+=1
        if flags&1:
            output.append(value);ring[position]=value;position=(position+1)&4095
        else:
            high=source[at];at+=1;offset=value|((high&0xf0)<<4)
            for n in range((high&15)+3):
                value=ring[(offset+n)&4095];output.append(value);ring[position]=value;position=(position+1)&4095
    if len(output)!=maximum or at!=len(source):raise ValueError('Resource length mismatch')
    return output

def main():
    destination=ROOT/'build/resource-load';destination.mkdir(parents=True,exist_ok=True)
    samples=[];portable_samples=[];evidence=[]
    for archive,indices in [('bak.pak',[10,100]),('char.pak',[0,64]),('script.pak',[1])]:
        path=ROOT/'res'/archive
        with path.open('rb') as source:
            header=source.read(16)
            if header[:4]!=b'KCAP':raise ValueError('Unexpected archive header')
            count=struct.unpack_from('<I',header,12)[0]
            for index in indices:
                if index>=count:raise ValueError('Missing sample member')
                source.seek(16+index*44);record=source.read(44)
                flags=struct.unpack_from('<I',record)[0];name=record[4:28].split(b'\0')[0].decode('ascii')
                offset,length=struct.unpack_from('<II',record,36)
                if flags!=1 or length<8 or offset+length>path.stat().st_size:raise ValueError('Invalid compressed member')
                source.seek(offset);payload=source.read(length)
                stored,expanded=struct.unpack_from('<II',payload)
                if stored!=length or expanded>32*1024*1024:raise ValueError('Invalid member sizes')
                compressed=payload[8:];expected=hashlib.sha256(decode(compressed,expanded)).hexdigest()
                label=(archive+'/'+name).encode('ascii')
                samples.append(struct.pack('<III',len(label),len(compressed),expanded)+label+expected.encode('ascii')+compressed)
                portable_expected=hashlib.sha256(decode(compressed,expanded,32)).hexdigest()
                portable_samples.append(struct.pack('<III',len(label),len(compressed),expanded)+label+portable_expected.encode('ascii')+compressed)
                evidence.append(dict(archive=str(path),member=name,offset=offset,stored_bytes=length,
                    compressed_sha256=hashlib.sha256(compressed).hexdigest(),decoded_bytes=expanded,decoded_sha256=expected))
    target=destination/'samples.bin';target.write_bytes(b'W2RSv1\0\0'+struct.pack('<I',len(samples))+b''.join(samples))
    portable_target=destination/'portable-samples.bin';portable_target.write_bytes(b'W2RSv1\0\0'+struct.pack('<I',len(portable_samples))+b''.join(portable_samples))
    proof=dict(schema='wa2.resource-samples.v1',fixture=str(target),fixture_sha256=hashlib.sha256(target.read_bytes()).hexdigest(),
        samples=evidence,original_pak_loop=0x457090,initial_ring_fill=0,independent_reference_decoder=True,screen_access=False,
        kcap_fixture=str(portable_target),kcap_fixture_sha256=hashlib.sha256(portable_target.read_bytes()).hexdigest(),kcap_dictionary_fill=32)
    (ROOT/'logs/resource-samples.json').write_text(json.dumps(proof,indent=2)+'\n')
    print(json.dumps(dict(samples=len(samples),decoded_bytes=sum(x['decoded_bytes'] for x in evidence),fixture=str(target)),indent=2))

if __name__=='__main__':main()
