"""Check the embedded immutable image and launcher error-path file protection."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import sys
import uuid

ROOT=Path(__file__).resolve().parent.parent
sys.path.insert(0,str(ROOT/'tools/python'))
import pefile

def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    program=ROOT/'out/WA2_SDL2_x64.exe';original=ROOT/'WA2_chs.exe'
    binary=program.read_bytes();image=original.read_bytes();offset=binary.find(image)
    assert offset>=0 and binary.find(image,offset+1)<0,'Immutable PE data must be embedded exactly once'
    native=pefile.PE(data=binary,fast_load=True)
    assert native.FILE_HEADER.Machine==0x8664 and native.OPTIONAL_HEADER.Magic==0x20b
    assert native.OPTIONAL_HEADER.Subsystem==2,'Double-clickable GUI subsystem required'
    case=ROOT/'build/embedded-launch-tests'/uuid.uuid4().hex;case.mkdir(parents=True)
    copied=case/'input.exe';copied.write_bytes(image);linked=case/'input-hardlink.exe';os.link(copied,linked)
    empty=case/'empty';empty.mkdir();results=[]
    for label,args,needle in [
        ('missing_resources',['--root',str(empty),'--report',str(case/'missing.json')],'Game resources not found'),
        ('native_report',['--root',str(ROOT/'res'),'--report',str(program)],'report must not overwrite'),
        ('native_report_parse_failure',['--root',str(ROOT/'res'),'--report',str(program),'--bad-option'],'missing option value'),
        ('input_report',[str(copied),'--report',str(copied)],'report must not overwrite'),
        ('input_hardlink_report',[str(copied),'--report',str(linked)],'report must not overwrite'),
        ('input_hardlink_parse_failure',[str(copied),'--report',str(linked),'--bad-option'],'missing option value'),
        ('input_hardlink_snapshot',[str(copied),'--snapshot',str(linked)],'snapshot must not overwrite'),
        ('native_snapshot',['--root',str(ROOT/'res'),'--snapshot',str(program),'--report',str(case/'snapshot.json')],'snapshot must not overwrite'),
    ]:
        outcome=subprocess.run([str(program),*args],cwd=case,capture_output=True,timeout=15)
        output=outcome.stderr.decode('utf-8',errors='replace')+outcome.stdout.decode('utf-8',errors='replace')
        assert outcome.returncode==2 and needle in output,(label,outcome.returncode,output)
        assert program.read_bytes()==binary and original.read_bytes()==image and copied.read_bytes()==image and linked.read_bytes()==image,label
        results.append({'case':label,'return_code':outcome.returncode,'executables_unchanged':True})
    result={'schema':'wa2.embedded-launch-tests.v1','output_sha256':digest(program),'input_sha256':digest(original),
            'embedded_image_offset':offset,'embedded_image_bytes':len(image),'embedded_sha256':hashlib.sha256(binary[offset:offset+len(image)]).hexdigest(),
            'original_sidecar_required':False,'gui_subsystem':True,'cases':results}
    (ROOT/'logs/embedded-launch-tests.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))

if __name__=='__main__':main()
