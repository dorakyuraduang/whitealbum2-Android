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
music={'duplicate_writes_avoided':119,'committed':1}
assert parse([line(1,'music_flag_saves',json.dumps(music))])['latest']['music_flag_saves']==music;cases+=1
final=[line(65,'native_glyph_commands','{"glyph_budget_deferrals":3}'),line(65,'text_local_control_flow','{"local_jumps":1024}'),line(65,'run_completion','{"game_thread_finished":true,"source_returned":true}')]
snapshot=parse([old,*final]);assert snapshot['latest']['native_glyph_commands']['glyph_budget_deferrals']==3 and snapshot['latest']['text_local_control_flow']['local_jumps']==1024 and snapshot['latest']['run_completion']['source_returned'];cases+=1
special='special-contents-aot'
startup='I/WA2IO (456): '+special+' #0 startup_stage {"stage":"game_startup","elapsed_ns":1234}'
failed='I/WA2IO (456): '+special+' #65 run_completion {"game_thread_finished":true,"source_returned":false}'
observed=reader.parse_records('\n'.join([old,startup,failed]),special)
assert observed['latest']['startup_stage']['stage']=='game_startup' and not observed['latest']['run_completion']['source_returned'] and 'native_resources' not in observed['latest'];cases+=1
restarted=reader.parse_records('\n'.join([failed,startup]),special)
assert 'run_completion' not in restarted['latest'] and restarted['latest']['startup_stage']['elapsed_ns']==1234;cases+=1
fresh=startup.replace('"game_startup"','"runtime"').replace('1234','0')
assert len(reader.parse_records('\n'.join([startup,fresh]),special)['events'])==1;cases+=1
sys.path.insert(0,str(root/'scripts'))
from compare_android_text import particle_work,validate_particles,weather_work,validate_weather
base=dict(frames=10,particle_commands=50,particle_tiles=55,particle_continuations=10,limit_60_fps=False)
last=dict(frames=20,particle_commands=1050,particle_tiles=1105,particle_continuations=710,limit_60_fps=False)
work=particle_work(base,last,False)
assert work['commands_per_frame']==100 and work['tiles_per_frame']==105 and work['continued']==700
assert particle_work(base,dict(last,particle_continuations=10),True)['continued']==0
assert particle_work(base,dict(last,particle_tiles=955),False)['tiles']==900
validate_particles([work,dict(work,commands_per_frame=108)])
def rejected(function,*args):
    try:function(*args)
    except ValueError:return True
    return False
assert rejected(particle_work,base,last,True)
assert rejected(particle_work,base,dict(last,particle_commands=50),False)
assert rejected(particle_work,base,dict(last,limit_60_fps=True),False)
assert rejected(particle_work,base,dict(last,particle_continuations=1011),False)
assert rejected(validate_particles,[work,dict(work,tiles_per_frame=125)])
weather_base=dict(uptime_ms=1000,sample_age_ms=120,frames=5,small_weather_updates=10,small_weather_particles=620,small_weather_batched_updates=7)
weather_last=dict(uptime_ms=11100,sample_age_ms=220,frames=105,small_weather_updates=110,small_weather_particles=6820,small_weather_batched_updates=107)
active=weather_work(weather_base,weather_last,False)
assert active['updates']==100 and active['particles_per_update']==62 and active['updates_per_second']==10 and active['particles_per_second']==620
assert weather_work(weather_base,dict(weather_last,small_weather_batched_updates=7),True)['batched_updates']==0
assert rejected(weather_work,weather_base,weather_last,True)
assert rejected(weather_work,weather_base,dict(weather_last,small_weather_batched_updates=7),False)
assert rejected(weather_work,weather_base,dict(weather_last,small_weather_updates=10),True)
assert validate_weather([active,active])==['per_second','per_frame']
more_frames=weather_work(weather_base,dict(weather_last,frames=205),False)
assert validate_weather([active,more_frames])==['per_second']
faster=weather_work(weather_base,dict(weather_last,frames=205,small_weather_updates=210,small_weather_particles=13020),False)
assert validate_weather([active,faster])==['per_frame']
assert rejected(validate_weather,[active,more_frames,faster])
assert rejected(validate_weather,[active,weather_work(weather_base,dict(weather_last,small_weather_particles=5620),False)])
report=dict(schema='wa2.counter-reader.v1',parser_cases=cases,particle_work_checked=True,weather_work_checked=True,
            game_launched=False,screen_access=False,passed=True)
if len(sys.argv)>1:Path(sys.argv[1]).write_text(json.dumps(report)+'\n',encoding='utf-8')
print(json.dumps(report),flush=True)
