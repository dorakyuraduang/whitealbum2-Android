"""Add an observed target, regenerate safely, and update only changed sources."""
from pathlib import Path
import argparse
import json
import shutil
import subprocess
import sys
import uuid

ROOT=Path(__file__).resolve().parent.parent

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('seed',type=lambda value:int(value,0),nargs='+')
    parser.add_argument('--source',default='observed native-dispatch target')
    args=parser.parse_args()
    profile_path=ROOT/'config'/'wa2.json';profile=json.loads(profile_path.read_text())
    seeds=set(profile['seeds'])|set(args.seed)
    profile['seeds']=sorted(seeds)
    for seed in args.seed:profile['seed_sources'][f'0x{seed:08x}']=args.source
    stage_root=ROOT/'build'/'generation-stage'/uuid.uuid4().hex
    stage_root.mkdir(parents=True)
    staged_profile=stage_root/'profile.json';staged_profile.write_text(json.dumps(profile,indent=2)+'\n')
    stage=stage_root/'project'
    subprocess.run([sys.executable,str(ROOT/'scripts'/'generate_project.py'),'--profile',str(staged_profile),'--out',str(stage)],check=True)
    destination=ROOT/'generated'/'wa2-win64';backup=stage_root/'previous'
    updated=[]
    for source in stage.iterdir():
        target=destination/source.name
        if target.exists() and target.read_bytes()==source.read_bytes():continue
        if target.exists():backup.mkdir(exist_ok=True);shutil.copy2(target,backup/target.name)
        shutil.copy2(source,target);updated.append(target.name)
    profile_path.write_text(json.dumps(profile,indent=2)+'\n')
    print(json.dumps({'updated':updated,'backup':str(backup)},indent=2))

if __name__=='__main__':main()
