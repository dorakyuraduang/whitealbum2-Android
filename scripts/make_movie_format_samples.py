"""Make tiny own-content movie fixtures; no game resources or screen pixels."""
import argparse
import io
import json
from pathlib import Path
import struct
from PIL import Image


class Bits:
    def __init__(self):self.bits=''
    def put(self,value,count):self.bits+=format(value,'0'+str(count)+'b')
    def ue(self,value):word=format(value+1,'b');self.bits+='0'*(len(word)-1)+word
    def se(self,value):self.ue(-2*value if value<=0 else 2*value-1)
    def align(self):self.bits+='0'*(-len(self.bits)%8)
    def data(self):self.align();return int(self.bits,2).to_bytes(len(self.bits)//8,'big')
    def rbsp(self):self.put(1,1);return self.data()


def nal(kind,payload):
    output=bytearray([kind]);zeros=0
    for value in payload:
        if zeros>=2 and value<=3:output.append(3);zeros=0
        output.append(value);zeros=zeros+1 if not value else 0
    return bytes(output)


def h264():
    b=Bits();b.put(66,8);b.put(0,8);b.put(10,8);b.ue(0);b.ue(0);b.ue(2);b.ue(0);b.put(0,1);b.ue(0);b.ue(0);b.put(1,1);b.put(1,1);b.put(0,1);b.put(0,1);sps=nal(0x67,b.rbsp())
    b=Bits();b.ue(0);b.ue(0);b.put(0,1);b.put(0,1);b.ue(0);b.ue(0);b.ue(0);b.put(0,1);b.put(0,2);b.se(0);b.se(0);b.se(0);b.put(1,1);b.put(0,1);b.put(0,1);pps=nal(0x68,b.rbsp())
    frames=[]
    for n in range(25):
        b=Bits();b.ue(0);b.ue(2);b.ue(0);b.put(0,4);b.ue(0);b.put(0,1);b.put(0,1);b.se(0);b.ue(1);b.ue(25);b.align()
        payload=b.data()+bytes([64+n])*256+bytes([128])*128+b'\x80';frames.append(nal(0x65,payload))
    return sps,pps,frames


def silent_aac():
    # AAC-LC stereo CPE, shared long window, zero spectral bands, ID_END.
    b=Bits();b.put(1,3);b.put(0,4);b.put(1,1);b.put(0,1);b.put(0,2);b.put(0,1);b.put(0,6);b.put(0,1);b.put(0,2)
    for _ in range(2):b.put(100,8);b.put(0,3)
    b.put(7,3);return b.data()


def atom(kind,data):return struct.pack('>I4s',len(data)+8,kind.encode())+data
def full(kind,data,flags=0):return atom(kind,struct.pack('>I',flags)+data)
def u32(*values):return struct.pack('>'+'I'*len(values),*values)
MATRIX=u32(0x10000,0,0,0,0x10000,0,0,0,0x40000000)


def descriptor(kind,data):
    if len(data)>=128:raise ValueError('Fixture descriptor is too large')
    return bytes([kind,len(data)])+data


def mp4(sps,pps,frames,aac):
    videos=[u32(len(frame))+frame for frame in frames];audios=[aac]*44
    ftyp=atom('ftyp',b'isom'+u32(512)+b'isomiso2avc1mp41');payload=b''.join(videos+audios);offset=len(ftyp)+8
    avcc=b'\x01'+sps[1:4]+b'\xff\xe1'+struct.pack('>H',len(sps))+sps+b'\x01'+struct.pack('>H',len(pps))+pps
    visual=b'\0'*6+struct.pack('>H',1)+b'\0'*16+struct.pack('>HH',16,16)+u32(0x480000,0x480000,0)+struct.pack('>H',1)+b'\0'*32+struct.pack('>Hh',24,-1)
    video_entry=atom('avc1',visual+atom('avcC',avcc))
    config=descriptor(4,b'\x40\x15'+b'\0'*3+u32(128000,128000)+descriptor(5,b'\x12\x10'))
    esds=full('esds',descriptor(3,b'\0\x02\0'+config+descriptor(6,b'\x02')))
    sound=b'\0'*6+struct.pack('>H',1)+b'\0'*8+struct.pack('>HHHHI',2,16,0,0,44100<<16)
    audio_entry=atom('mp4a',sound+esds)
    def track(identifier,entry,samples,timescale,duration,delta,start,video):
        tkhd=full('tkhd',u32(0,0,identifier,0,1022)+b'\0'*8+struct.pack('>hhhh',0,0,0 if video else 256,0)+MATRIX+u32(16<<16 if video else 0,16<<16 if video else 0),7)
        mdhd=full('mdhd',u32(0,0,timescale,duration)+struct.pack('>HH',0x55c4,0))
        hdlr=full('hdlr',u32(0)+(b'vide' if video else b'soun')+b'\0'*12+b'Own fixture\0')
        media_header=full('vmhd',b'\0'*8,1) if video else full('smhd',b'\0'*4)
        dinf=atom('dinf',full('dref',u32(1)+full('url ',b'',1)))
        stbl=full('stsd',u32(1)+entry)+full('stts',u32(1,len(samples),delta))+full('stsc',u32(1,1,len(samples),1))
        stbl+=full('stsz',u32(0,len(samples))+u32(*map(len,samples)))+full('stco',u32(1,start))
        if video:stbl+=full('stss',u32(len(samples),*range(1,len(samples)+1)))
        return atom('trak',tkhd+atom('mdia',mdhd+hdlr+atom('minf',media_header+dinf+atom('stbl',stbl))))
    mvhd=full('mvhd',u32(0,0,1000,1022,0x10000)+struct.pack('>HH',256,0)+b'\0'*8+MATRIX+b'\0'*24+u32(3))
    moov=atom('moov',mvhd+track(1,video_entry,videos,1000,1000,40,offset,True)+track(2,audio_entry,audios,44100,45056,1024,offset+sum(map(len,videos)),False))
    return ftyp+atom('mdat',payload)+moov


def flv(sps,pps,frames,aac):
    out=b'FLV\x01\x05'+u32(9,0)
    def tag(kind,data,time):
        size=len(data);header=bytes([kind])+size.to_bytes(3,'big')+(time&0xffffff).to_bytes(3,'big')+bytes([time>>24])+b'\0'*3
        return header+data+u32(len(header)+len(data))
    avcc=b'\x01'+sps[1:4]+b'\xff\xe1'+struct.pack('>H',len(sps))+sps+b'\x01'+struct.pack('>H',len(pps))+pps
    out+=tag(9,b'\x17\x00\0\0\0'+avcc,0)+tag(8,b'\xaf\x00\x12\x10',0)
    packets=[(n*40,9,b'\x17\x01\0\0\0'+u32(len(frame))+frame) for n,frame in enumerate(frames)]
    packets += [(n*1024*1000//44100,8,b'\xaf\x01'+aac) for n in range(44)]
    for time,kind,data in sorted(packets):out+=tag(kind,data,time)
    return out


def riff(kind,data):return kind+struct.pack('<I',len(data))+data+(b'\0' if len(data)%2 else b'')


def avi(channels=2):
    buffer=io.BytesIO();Image.new('RGB',(16,16),(80,120,160)).save(buffer,format='JPEG',quality=90,subsampling=0);jpeg=buffer.getvalue()
    avih=struct.pack('<14I',40000,0,0,16,25,0,2,8192,16,16,0,0,0,0)
    def stream(kind,handler,scale,rate,length,sample_size,fmt):
        header=struct.pack('<4s4sIHHIIIIIIIIhhhh',kind,handler,0,0,0,0,scale,rate,0,length,8192,0xffffffff,sample_size,0,0,16 if kind==b'vids' else 0,16 if kind==b'vids' else 0)
        return riff(b'LIST',b'strl'+riff(b'strh',header)+riff(b'strf',fmt))
    bitmap=struct.pack('<IiiHH4sIIii',40,16,16,1,24,b'MJPG',16*16*3,0,0,0)+struct.pack('<I',0)
    align=channels*2;wave=struct.pack('<HHIIHH',1,channels,48000,48000*align,align,16)
    hdrl=riff(b'LIST',b'hdrl'+riff(b'avih',avih)+stream(b'vids',b'MJPG',1,25,25,0,bitmap)+stream(b'auds',b'\0'*4,1,48000,48000,align,wave))
    chunks=b'';index=b''
    for n in range(25):
        pcm=b''.join(struct.pack('<'+'h'*channels,((n*1920+i)%101-50)*20,*([0]*(channels-1))) for i in range(1920))
        for kind,data,flags in ((b'00dc',jpeg,16),(b'01wb',pcm,16)):
            index+=struct.pack('<4sIII',kind,flags,4+len(chunks),len(data));chunks+=riff(kind,data)
    return riff(b'RIFF',b'AVI '+hdrl+riff(b'LIST',b'movi'+chunks)+riff(b'idx1',index))


def transport_stream(sps,pps,frames,aac):
    counts={};out=bytearray()
    def packet(pid,payload,pusi):
        counter=counts.get(pid,0);counts[pid]=(counter+1)%16
        if len(payload)<184:
            padding=183-len(payload);adapt=bytes([padding])+(b'\0'+b'\xff'*(padding-1) if padding else b'');mode=0x30
        else:adapt=b'';mode=0x10
        out.extend(bytes([0x47,(pid>>8)|(0x40 if pusi else 0),pid&255,mode|counter])+adapt+payload)
    def section(pid,data):
        crc=0xffffffff
        for byte in data:
            crc^=byte<<24
            for _ in range(8):crc=((crc<<1)^0x04c11db7 if crc&0x80000000 else crc<<1)&0xffffffff
        packet(pid,b'\0'+data+u32(crc),True)
    section(0,bytes.fromhex('00 b0 0d 00 01 c1 00 00 00 01 e1 00'))
    section(0x100,bytes.fromhex('02 b0 17 00 01 c1 00 00 e1 01 f0 00 1b e1 01 f0 00 0f e1 02 f0 00'))
    def pes(pid,stream,data,pts):
        stamp=bytes([0x21|((pts>>29)&14),(pts>>22)&255,1|((pts>>14)&254),(pts>>7)&255,1|((pts<<1)&254)])
        body=b'\0\0\x01'+bytes([stream])+struct.pack('>H',len(data)+8)+b'\x80\x80\x05'+stamp+data
        for at in range(0,len(body),184):packet(pid,body[at:at+184],at==0)
    packets=[]
    for n,frame in enumerate(frames):packets.append((n*3600,0x101,0xe0,b'\0\0\0\x01'+sps+b'\0\0\0\x01'+pps+b'\0\0\0\x01'+frame))
    length=len(aac)+7
    adts=bytes([0xff,0xf1,0x50,0x80|((length>>11)&3),(length>>3)&255,((length&7)<<5)|31,0xfc])+aac
    for n in range(44):packets.append((n*1024*90000//44100,0x102,0xc0,adts))
    for time,pid,stream,data in sorted(packets):pes(pid,stream,data,time)
    return bytes(out)


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--out',type=Path,required=True);args=parser.parse_args();args.out.mkdir(parents=True,exist_ok=True)
    sps,pps,frames=h264();aac=silent_aac()
    samples={'own-h264-aac.mp4':mp4(sps,pps,frames,aac),'own-h264-aac.flv':flv(sps,pps,frames,aac),'own-mjpeg-pcm.avi':avi(),'own-mjpeg-surround.avi':avi(6),'own-h264-aac.ts':transport_stream(sps,pps,frames,aac)}
    for name,data in samples.items():(args.out/name).write_bytes(data)
    (args.out/'own-mp4-renamed.pak').write_bytes(samples['own-h264-aac.mp4'])
    print(json.dumps({'samples':{name:len(data) for name,data in samples.items()},'own_content_only':True,'screen_access':False}))


if __name__=='__main__':main()
