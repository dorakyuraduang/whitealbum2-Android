"""Export external SC English resources from the user's full patch ZIP.

No game is launched. All output stays external; no PAK/caption data is packaged
in the APK or published. Exact Vorbis packets, rather than video filenames,
identify the original drama/chapter. One AAC replacement uses a SHA-bound
3970 ms offset measured at three disjoint windows by finite audio correlation.
"""
from __future__ import annotations
import argparse
import hashlib
import html
import json
from pathlib import Path
import re
import struct
import zipfile
from prepare_special_english import prepare,sha,ROOT
from special_mkv_subtitles import inspect,ogg

AAC_NAME='Audio Drama/Bedtime Stories - Crankiness of a Faithful Dog.mkv'
AAC_SHA='6e1167cb0d034dc9dc69854a84d502ba994fce1e18fd997314a9837ebb86fe92'
AAC_BASE_SHA='84e129b7928c8cc0c909da65620637378ba84cec1f91dac6a76846e6178f6a70'
EN_PAK_SHA='c13ecdf5a43d1e42f32400c491189aa2a39386a468d3b12e226211affe31da99'

def audio_sources(root):
    result={}
    with (root/'BGM.PAK').open('rb') as f:
        header=f.read(8)
        if header[:4]!=b'LAC\0':raise ValueError('Expected original SC LAC BGM.PAK')
        count=struct.unpack_from('<I',header,4)[0]
        if not 1<=count<=4096:raise ValueError('SC BGM index bound')
        index=f.read(count*40)
        if len(index)!=count*40:raise ValueError('Truncated SC BGM index')
        length=f.seek(0,2)
        for at in range(0,len(index),40):
            raw=index[at:at+40];name=bytes(n^255 for n in raw[:31].split(b'\0',1)[0]).decode('ascii')
            match=re.fullmatch(r'BGM_(08[0-5])(?:_([A-G]))?\.OGG',name)
            if not match:continue
            size,offset=struct.unpack_from('<II',raw,32)
            if raw[31] or not 1<=size<=100*1024*1024 or offset+size>length:raise ValueError('Unsupported SC BGM member')
            f.seek(offset);data=f.read(size);row=ogg(data);row['sha256']=sha(data);row['key']=(int(match[1])<<16)|(ord(match[2])-65 if match[2] else 0);result[name]=row
    if len(result)!=24:raise ValueError('Expected the 24 original SC drama chapters')
    return result

def plain(text):
    text=text.replace('\r\n','\n').replace('\r','\n')
    text=re.sub(r'</?(?:i|b|u)>','',text,flags=re.I)
    text=re.sub(r'<font size="36">|</font>','',text,flags=re.I)
    if re.search(r'<[^>]+>',text):raise ValueError('Unsupported subtitle markup')
    text=html.unescape(text).strip()
    if not text or len(text)>512 or any((ord(c)<32 and c!='\n') or 127<=ord(c)<160 or ord(c)==0xfeff for c in text):raise ValueError('Unsupported subtitle text')
    return text

def merge(cues):
    # Preserve simultaneous captions, then coalesce identical adjacent states.
    events={}
    for n,(a,b,t) in enumerate(cues):
        if a<0 or a>=b:raise ValueError('Invalid subtitle interval')
        events.setdefault(a,[]).append((True,n,t));events.setdefault(b,[]).append((False,n,t))
    active={};result=[];times=sorted(events)
    for n,time in enumerate(times[:-1]):
        for add,key,text in events[time]:
            if add:active[key]=text
            else:active.pop(key,None)
        if not active:continue
        text='\n'.join(dict.fromkeys(active[k] for k in sorted(active)))
        if len(text)>512:raise ValueError('Combined SC cue exceeds its bound')
        end=times[n+1]
        if result and result[-1][1]==time and result[-1][2]==text:result[-1][1]=end
        else:result.append([time,end,text])
    return result

def captions(z,sources):
    names=[n for n in z.namelist() if n.lower().endswith('.mkv')]
    if len(names)!=11:raise ValueError('Expected eleven English drama MKVs')
    tracks={};proof=[]
    for name in names:
        item=z.getinfo(name)
        if item.file_size>160*1024*1024:raise ValueError('English MKV exceeds bound')
        data=z.read(item);parsed=inspect(data);audio=parsed['audio'];packets=[p[0] for p in audio];matches=[]
        # The first Vorbis packet is often shared silence; packet 16 is a more
        # selective anchor. Always compare the entire source packet sequence.
        positions={}
        for n,p in enumerate(packets):positions.setdefault(p,[]).append(n)
        for source,base in sources.items():
            original=base['audio'];anchor=min(16,len(original)-1)
            found=[p-anchor for p in positions.get(original[anchor],[]) if p>=anchor and packets[p-anchor:p-anchor+len(original)]==original]
            if len(found)>1:raise ValueError('Ambiguous SC audio identity')
            if found:
                at=found[0];begin=audio[at][1]
                if begin is None:raise ValueError('Chapter begins inside a lace without a timestamp')
                matches.append(dict(source=source,start=round(begin),duration=round(base['duration']),packet_offset=at,packet_count=len(original),identity='all-vorbis-packets'))
        if name==AAC_NAME:
            if matches or sha(data)!=AAC_SHA or sources['BGM_085_B.OGG']['sha256']!=AAC_BASE_SHA:raise ValueError('Unsupported AAC calibration input')
            matches=[dict(source='BGM_085_B.OGG',start=3970,duration=round(sources['BGM_085_B.OGG']['duration']),identity='sha-bound-three-window-audio-correlation',resolution_ms=10)]
        if not matches:raise ValueError('Cannot map English drama audio: '+name)
        matches.sort(key=lambda x:x['start'])
        for n,m in enumerate(matches):
            key=sources[m['source']]['key']
            if key in tracks:raise ValueError('Duplicate SC chapter')
            begin=m['start'];end=matches[n+1]['start'] if n+1<len(matches) else begin+m['duration']
            split=[]
            for a,b,text in parsed['captions']:
                if a<end and b>begin:split.append([max(a,begin)-begin,min(b,end)-begin,plain(text)])
            tracks[key]=merge(split)
        proof.append(dict(file=name,sha256=sha(data),captions=len(parsed['captions']),chapter_matches=matches))
    if len(tracks)!=24 or any(not c for c in tracks.values()):raise ValueError('English drama coverage incomplete')
    return tracks,proof

def encode(tracks):
    output=bytearray(b'SCU1'+struct.pack('<II',2,len(tracks)));cues=points=0;charset=set()
    for key,rows in sorted(tracks.items()):
        output.extend(struct.pack('<II',key,len(rows)))
        for a,b,t in rows:
            raw=t.encode('utf-8');output.extend(struct.pack('<III',a,b,len(raw)));output.extend(raw);cues+=1;points+=len(t);charset.update(t.replace('\n',''))
    if len(output)>1024*1024 or cues>8192 or points>262144 or len(charset)>1536:raise ValueError('SC subtitle output exceeds shared decoder bounds')
    return bytes(output),dict(tracks=len(tracks),cues=cues,codepoints=points,characters=len(charset))

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('patch',type=Path);p.add_argument('--base-root',type=Path,required=True);p.add_argument('--output',type=Path,default=ROOT/'out/special-english-resources');args=p.parse_args()
    prepare(args.patch);sources=audio_sources(args.base_root);output=args.output.resolve()
    if not output.is_relative_to(ROOT/'out'):raise ValueError('Output must stay in the workspace out directory')
    output.mkdir(parents=True,exist_ok=True)
    with zipfile.ZipFile(args.patch) as z:
        en=z.read('en.pak')
        if sha(en)!=EN_PAK_SHA:raise ValueError('Unsupported SC English resource pack')
        tracks,proof=captions(z,sources);subtitles,counts=encode(tracks)
        files={'en.pak':en,'subtext_en.dat':subtitles}
        for src,dest in [('mv000.pak','mv000_en.pak'),('credits0.pak','credits_en.pak')]:
            if z.getinfo(src).file_size>160*1024*1024:raise ValueError('English movie exceeds bound')
            files[dest]=z.read(src)
    resources={}
    for name,data in files.items():
        path=output/name
        if path.exists() and path.read_bytes()!=data:raise ValueError('Refusing to overwrite different exported data: '+name)
        path.write_bytes(data);resources[name]=dict(size=len(data),sha256=sha(data))
    receipt=dict(schema='wa2.special-english-resources.v1',resources=resources,subtitles=counts,audio_mapping=proof,game_launched=False,screen_access=False,device_gameplay_verified=False)
    (ROOT/'logs/special-english-resources.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(dict(output=str(output),subtitles=counts,resources=resources)),flush=True)

if __name__=='__main__':main()
