"""Build an internal codec fixture from three unmodified voice Ogg members."""
from pathlib import Path
import hashlib
import json
import struct
ROOT=Path(__file__).resolve().parents[1]

def main():
    source=ROOT/'res/VOICE.PAK';samples=[];payloads=[]
    with source.open('rb') as stream:
        header=stream.read(8);assert header[:4]==b'LAC\0';count=struct.unpack_from('<I',header,4)[0]
        for index in [0,count//2,count-1]:
            stream.seek(8+index*40);record=stream.read(40);size,offset=struct.unpack_from('<II',record,32)
            assert record[31]==0 and size>=4096
            stream.seek(offset);payload=stream.read(size);assert len(payload)==size and payload[:4]==b'OggS'
            packet=27+payload[26];assert payload[packet:packet+7]==b'\x01vorbis'
            samples.append(dict(source_index=index,offset=offset,size=size,sha256=hashlib.sha256(payload).hexdigest(),
                name=f'voice-{index}.ogg',channels=payload[packet+11],rate=struct.unpack_from('<I',payload,packet+12)[0]))
            payloads.append(payload)
    directory=ROOT/'build/native-voice-codec';directory.mkdir(parents=True,exist_ok=True)
    fixture=directory/'BGM.PAK';offset=16+len(samples)*44;table=[]
    for sample in samples:
        table.append(struct.pack('<I24s8xII',0,sample['name'].encode(),offset,sample['size']));offset+=sample['size']
    fixture.write_bytes(struct.pack('<4sIII',b'KCAP',0,0,len(samples))+b''.join(table)+b''.join(payloads))
    proof=dict(schema='wa2.voice-codec-samples.v1',source=str(source),source_bytes=source.stat().st_size,
        fixture=str(fixture),fixture_sha256=hashlib.sha256(fixture.read_bytes()).hexdigest(),samples=samples,original_payloads_unchanged=True)
    (ROOT/'logs/voice-codec-samples.json').write_text(json.dumps(proof,indent=2)+'\n')
    print(json.dumps(dict(samples=len(samples),channels=[v['channels'] for v in samples],rates=[v['rate'] for v in samples],fixture=str(fixture))))

if __name__=='__main__':main()
