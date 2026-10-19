"""Create a resource/profile snapshot for comparison without changing saves."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import uuid

ROOT=Path(__file__).resolve().parents[1]


def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile',type=Path,required=True)
    parser.add_argument('--evidence',type=Path,required=True)
    args=parser.parse_args();source=(ROOT/'res').resolve();profile=args.profile.resolve()
    target=ROOT/'build/matched-performance'/uuid.uuid4().hex/'res';target.mkdir(parents=True)
    linked=[];copied=[]
    for path in source.rglob('*'):
        relative=path.relative_to(source)
        if not path.is_file() or relative.parts[0].lower()=='documents' or path.suffix.lower() in ('.exe','.cmd','.json','.log'):continue
        if not path.resolve().is_relative_to(source):raise ValueError('Resource link escapes source')
        output=target/relative;output.parent.mkdir(parents=True,exist_ok=True)
        if path.suffix.lower()=='.pak':os.link(path,output);linked.append(dict(file=str(relative),bytes=path.stat().st_size))
        else:shutil.copy2(path,output);copied.append(dict(file=str(relative),sha256=digest(output)))
    snapshots=[]
    for name in ('D3D.ini','SYSTEM.ini','Sys.sav'):
        path=profile/name;output=target/'Documents/Leaf/WHITE ALBUM2'/name;output.parent.mkdir(parents=True,exist_ok=True)
        expected=digest(path);shutil.copy2(path,output)
        if digest(path)!=expected or digest(output)!=expected:raise ValueError('Profile changed during snapshot')
        snapshots.append(dict(source=str(path),file=str(output),sha256=expected))
    record=dict(schema='wa2.matched-root.v1',root=str(target),resources=str(source),native_profile=str(profile),
        linked_resources=linked,copied_resources=copied,profile_snapshots=snapshots,source_profile_modified=False)
    args.evidence.write_text(json.dumps(record,indent=2)+'\n');print(json.dumps(dict(root=str(target),profile_files=len(snapshots),resources=len(linked))))


if __name__=='__main__':main()
