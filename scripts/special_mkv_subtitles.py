"""Bounded Matroska/SRT and Ogg packet inspection, without media playback.

Container/lacing reference: https://www.matroska.org/technical/notes.html
Only the English SC patch's single S_TEXT/UTF8 track is supported. Audio packet
hashes establish chapter identity; HEVC video payloads are skipped untouched.
"""
from __future__ import annotations
import hashlib
import math
import struct

def vint(data,at,identifier=False):
    if at>=len(data):raise ValueError('Truncated EBML integer')
    first=data[at];width=1
    while width<=8 and not first&(1<<(8-width)):width+=1
    if width>8 or (identifier and width>4) or at+width>len(data):raise ValueError('Invalid EBML integer')
    value=int.from_bytes(data[at:at+width],'big')
    return (value if identifier else value&((1<<(width*7))-1)),at+width,width

def elements(data,start=0,end=None):
    end=len(data) if end is None else end
    while start<end:
        key,start,_=vint(data,start,True);size,start,width=vint(data,start)
        stop=end if size==(1<<(7*width))-1 else start+size
        if stop>end or start>end:raise ValueError('Truncated EBML element')
        yield key,start,stop
        start=stop

def block(data,start,end):
    track,p,_=vint(data,start)
    if p+3>end:raise ValueError('Truncated Matroska block')
    time=struct.unpack_from('>h',data,p)[0];flags=data[p+2];p+=3;lace=(flags>>1)&3
    if not lace:return track,time,[data[p:end]]
    if p>=end:raise ValueError('Truncated Matroska lace')
    count=data[p]+1;p+=1;sizes=[]
    if lace==2:
        if (end-p)%count:raise ValueError('Invalid fixed lace')
        sizes=[(end-p)//count]*count
    elif lace==1:
        for _ in range(count-1):
            size=0
            while True:
                if p>=end:raise ValueError('Truncated Xiph lace')
                n=data[p];p+=1;size+=n
                if n<255:break
            sizes.append(size)
    else:
        if count<2:raise ValueError('Invalid EBML lace count')
        n,p,_=vint(data,p);sizes=[n]
        for _ in range(count-2):
            n,p,width=vint(data,p);sizes.append(sizes[-1]+n-((1<<(7*width-1))-1))
    if len(sizes)<count:sizes.append(end-p-sum(sizes))
    packets=[]
    for size in sizes:
        if size<0 or p+size>end:raise ValueError('Invalid lace frame')
        packets.append(data[p:p+size]);p+=size
    if p!=end:raise ValueError('Trailing lace data')
    return track,time,packets

def inspect(data):
    if not data or len(data)>160*1024*1024:raise ValueError('MKV exceeds its bound')
    segments=[(a,b) for key,a,b in elements(data) if key==0x18538067]
    if len(segments)!=1:raise ValueError('Expected one Matroska segment')
    top=list(elements(data,*segments[0]));tracks={};scale=1000000;duration=0
    for key,a,b in top:
        if key==0x1549a966:
            for k,p,e in elements(data,a,b):
                if k==0x2ad7b1:scale=int.from_bytes(data[p:e],'big')
                if k==0x4489:
                    if e-p not in (4,8):raise ValueError('Invalid Matroska duration')
                    duration=struct.unpack('>d' if e-p==8 else '>f',data[p:e])[0]
        if key==0x1654ae6b:
            for k,p,e in elements(data,a,b):
                if k!=0xae:continue
                fields={k:data[x:y] for k,x,y in elements(data,p,e)};number=int.from_bytes(fields[0xd7],'big')
                if number in tracks or len(tracks)>=8:raise ValueError('Duplicate/excessive Matroska tracks')
                if 0x23314f in fields and fields[0x23314f] not in (struct.pack('>f',1),struct.pack('>d',1)):raise ValueError('Unsupported TrackTimestampScale')
                tracks[number]=dict(codec=fields.get(0x86,b'').decode(),language=fields.get(0x22b59c,b'und').decode(),delay=int.from_bytes(fields.get(0x56aa,b'\0'),'big'))
    text_tracks=[n for n,t in tracks.items() if t['codec'].startswith('S_')]
    audio_tracks=[n for n,t in tracks.items() if t['codec'].startswith('A_')]
    if len(text_tracks)!=1 or len(audio_tracks)!=1:raise ValueError('Expected one text and one audio track')
    text=text_tracks[0];audio_track=audio_tracks[0]
    if tracks[text]['codec']!='S_TEXT/UTF8' or tracks[text]['language'] not in ('eng','und'):raise ValueError('Unsupported SC subtitle track')
    if tracks[audio_track]['codec'] not in ('A_VORBIS','A_AAC'):raise ValueError('Unsupported SC audio codec')
    if not 0<scale<=1000000000 or not math.isfinite(duration) or not 0<duration*scale<=86400000000000:raise ValueError('Invalid Matroska time scale/duration')
    captions=[];audio=[]
    for key,a,b in top:
        if key!=0x1f43b675:continue
        cluster=list(elements(data,a,b));clocks=[int.from_bytes(data[p:e],'big') for k,p,e in cluster if k==0xe7]
        if len(clocks)!=1:raise ValueError('Invalid cluster timestamp')
        for k,p,e in cluster:
            period=None
            if k==0xa0:
                fields={key:(x,y) for key,x,y in elements(data,p,e)}
                if 0xa1 not in fields:raise ValueError('BlockGroup lacks a block')
                if 0x9b in fields:x,y=fields[0x9b];period=int.from_bytes(data[x:y],'big')
                p,e=fields[0xa1]
            elif k!=0xa3:continue
            # Video blocks are skipped before copying a potentially large frame.
            number,_,_=vint(data,p)
            if number not in tracks:raise ValueError('Unknown Matroska track')
            if number not in (text,audio_track):continue
            track,time,packets=block(data,p,e);begin=((clocks[0]+time)*scale-tracks[track]['delay'])/1000000
            if track==audio_track and tracks[track]['codec']=='A_VORBIS':
                audio.extend((hashlib.sha256(q).hexdigest(),begin if n==0 else None) for n,q in enumerate(packets))
            if track==text:
                if period is None or len(packets)!=1 or not 0<period*scale<=86400000000000 or len(packets[0])>4096:raise ValueError('Unsupported subtitle timing/lacing')
                captions.append([round(begin),round(begin+period*scale/1000000),packets[0].decode('utf-8')])
                if len(captions)>8192:raise ValueError('MKV subtitle count exceeded')
    if not captions:raise ValueError('No SC subtitles found')
    return dict(duration=duration*scale/1000000,tracks=tracks,captions=captions,audio=audio)

def ogg(data):
    p=0;packet=bytearray();packets=[];samples=0
    while p<len(data):
        if p+27>len(data) or data[p:p+5]!=b'OggS\0':raise ValueError('Invalid Ogg page')
        count=data[p+26];sizes=data[p+27:p+27+count];at=p+27+count
        if len(sizes)!=count or at+sum(sizes)>len(data):raise ValueError('Truncated Ogg page')
        granule=int.from_bytes(data[p+6:p+14],'little')
        if granule!=0xffffffffffffffff:samples=granule
        for n in sizes:
            packet.extend(data[at:at+n]);at+=n
            if len(packet)>1024*1024:raise ValueError('Ogg packet bound exceeded')
            if n<255:packets.append(bytes(packet));packet.clear()
        p=at
    if packet or len(packets)<4 or packets[0][:7]!=b'\x01vorbis' or len(packets[0])<16:raise ValueError('Incomplete Vorbis stream')
    rate=struct.unpack_from('<I',packets[0],12)[0]
    if not 8000<=rate<=192000:raise ValueError('Invalid Vorbis rate')
    return dict(duration=samples*1000/rate,audio=[hashlib.sha256(q).hexdigest() for q in packets[3:]])
