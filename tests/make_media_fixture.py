"""Small uncompressed AVI, independent of game assets and third-party codecs."""
from pathlib import Path
import struct
import sys
def chunk(kind,data):return kind+struct.pack('<I',len(data))+data+(b'\0' if len(data)%2 else b'')
def listing(kind,data):return chunk(b'LIST',kind+data)
width=height=64
frame=bytes([0x33,0x22,0x11])*(width*height)
avih=struct.pack('<14I',100000,len(frame)*10,0,0x10,3,0,1,len(frame),width,height,0,0,0,0)
strh=struct.pack('<4s4sIHH8I4h',b'vids',b'DIB ',0,0,0,0,1,10,0,3,len(frame),0xffffffff,0,0,0,width,height)
strf=struct.pack('<IiiHHIIiiII',40,width,height,1,24,0,len(frame),0,0,0,0)
header=listing(b'hdrl',chunk(b'avih',avih)+listing(b'strl',chunk(b'strh',strh)+chunk(b'strf',strf)))
frames=b''.join(chunk(b'00db',frame) for _ in range(3))
index=b''.join(struct.pack('<4sIII',b'00db',0x10,4+n*(len(frame)+8),len(frame)) for n in range(3))
data=b'AVI '+header+listing(b'movi',frames)+chunk(b'idx1',index)
path=Path(sys.argv[1]);path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(chunk(b'RIFF',data))
print(path)
