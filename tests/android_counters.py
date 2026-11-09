"""Counter transport parser fixtures only; no ADB or device calls."""
import importlib.util
import json
from pathlib import Path
import sys
root=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('counter_reader',root/'scripts/read_android_counters.py');reader=importlib.util.module_from_spec(spec);spec.loader.exec_module(reader)
def parse(lines):return reader.parse_records('\n'.join(lines),'fixture')
def line(seq,key,value):return 'I/WA2IO (123): fixture #'+str(seq)+' '+key+' '+value
cases=0
value={'loads':321,'timing':['x'*1200,'白'*400]};body=json.dumps(value,ensure_ascii=False);pieces=[body[n:n+200] for n in range(0,len(body),200)]
parts=[line(3,'native_resources','part '+str(n+1)+'/'+str(len(pieces))+' '+piece) for n,piece in enumerate(pieces)]
assert parse(parts)['latest']['native_resources']==value;cases+=1
assert parse(parts[:-1])['incomplete_records']==1 and 'native_resources' not in parse(parts[:-1])['latest'];cases+=1
assert parse(list(reversed(parts)))['latest']['native_resources']==value;cases+=1
assert parse([parts[0],parts[0],*parts[1:]])['latest']['native_resources']==value;cases+=1
old=line(1,'native_resources','{"loads":0}')
truncated=line(2,'native_resources','{"loads":10,"timing":[')
assert 'native_resources' not in parse([old,truncated])['latest'];cases+=1
assert parse([old,*parts])['latest']['native_resources']==value;cases+=1
assert parse([*parts,old])['latest']['native_resources']=={'loads':0};cases+=1
assert not parse([line(1,'native_resources','part 1/129 {}')])['available'];cases+=1
assert not parse([line(1,'native_resources','part 1/1 '+('x'*641))])['available'];cases+=1
assert not parse([line(1,'native_resources','[]')])['available'];cases+=1
assert not parse([line(1,'unknown_key','{}')])['available'];cases+=1
assert not parse([old.replace('fixture','other-release')])['available'];cases+=1
report=dict(schema='wa2.counter-reader.v1',parser_cases=cases,game_launched=False,screen_access=False,passed=True)
if len(sys.argv)>1:Path(sys.argv[1]).write_text(json.dumps(report)+'\n',encoding='utf-8')
print(json.dumps(report),flush=True)
