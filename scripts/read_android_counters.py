"""Read only WA2IO counters from one authorized app UID; never launches it."""
import argparse
import json
from pathlib import Path
import re
import subprocess

KEYS={'selected_directory_io','save_read_scope','native_resources','scheduler','host_event_pump','frame_work','static_cpu_work','native_image_chroma'}

def parse_records(text,release):
    pattern=re.compile(re.escape(release)+r' #(\d+) (\w+) (.*)')
    latest={};latest_sequence={};events=[];pending={};previous=0;incomplete=0
    for line in text.splitlines():
        match=pattern.search(line)
        if not match:continue
        sequence=int(match[1]);key=match[2];body=match[3]
        if key not in KEYS:continue
        if sequence<previous:latest.clear();latest_sequence.clear();events.clear();pending.clear();incomplete=0
        previous=sequence
        if sequence>=latest_sequence.get(key,0):latest.pop(key,None);latest_sequence[key]=sequence
        chunk=re.fullmatch(r'part (\d+)/(\d+) (.*)',body)
        if chunk:
            part,total=int(chunk[1]),int(chunk[2]);piece=chunk[3]
            if not 1<=part<=total<=128 or len(piece.encode('utf-8'))>640:incomplete+=1;continue
            group=pending.setdefault((sequence,key),dict(total=total,pieces={}))
            if group['total']!=total or (part in group['pieces'] and group['pieces'][part]!=piece):pending.pop((sequence,key),None);incomplete+=1;continue
            group['pieces'][part]=piece
            if len(pending)>8:pending.pop(next(iter(pending)));incomplete+=1
            if len(group['pieces'])!=total:continue
            body=''.join(group['pieces'][n] for n in range(1,total+1));pending.pop((sequence,key),None)
        if len(body.encode('utf-8'))>65536:incomplete+=1;continue
        try:data=json.loads(body)
        except ValueError:incomplete+=1;continue
        if not isinstance(data,dict):incomplete+=1;continue
        latest[key]=data;events.append(dict(sequence=sequence,key=key,data=data))
    return dict(release=release,available=bool(latest),latest=latest,events=events,incomplete_records=incomplete+len(pending),game_launched=False,screen_access=False,process_taken_over=False)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial',required=True);parser.add_argument('--adb',type=Path,default=Path('tools/android-sdk/platform-tools/adb.exe'))
    parser.add_argument('--output',type=Path,default=Path('logs/android-live-io.json'));parser.add_argument('--release')
    args=parser.parse_args();root=Path(__file__).resolve().parents[1]
    release=args.release or json.loads((root/'config/release.json').read_text())['name']
    flags=getattr(subprocess,'CREATE_NO_WINDOW',0)|getattr(subprocess,'BELOW_NORMAL_PRIORITY_CLASS',0)
    def call(command):return subprocess.run([str(args.adb),'-s',args.serial,*command],creationflags=flags,capture_output=True,check=True,timeout=20).stdout.decode('utf-8',errors='replace')
    uid=re.search(r'^package:org\.wa2\.port uid:(\d+)\s*$',call(['shell','cmd','package','list','packages','-U','org.wa2.port']),re.M)
    if not uid:raise RuntimeError('Exact WA2 app UID is unavailable')
    log=call(['logcat','-d','--uid='+uid[1],'-v','brief','WA2IO:I','*:S'])
    proof=parse_records(log,release);args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(proof,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({key:value for key,value in proof.items() if key!='events'}),flush=True)
if __name__=='__main__':main()
