"""Fetch pinned, local-only tools for recovering the remaining engine source.

These tools are optional analysis dependencies, never game runtime inputs.
Archives are verified before extraction; no machine-wide installation occurs.
"""
from pathlib import Path
import hashlib
import json
import shutil
import urllib.request
import zipfile
ROOT=Path(__file__).resolve().parents[1]
PACKAGES=[dict(name='ghidra',archive='ghidra_12.1.4_PUBLIC_20260921.zip',
    url='https://github.com/NationalSecurityAgency/ghidra/releases/download/Ghidra_12.1.4_build/ghidra_12.1.4_PUBLIC_20260921.zip',
    sha256='ddac49f903da9d5bac833e5cc79395098b9c33cfd3279be5f31bd00387d2d4db',root='ghidra_12.1.4_PUBLIC'),
    dict(name='jdk',archive='OpenJDK21U-jdk_x64_windows_hotspot_21.0.12.1_1.zip',
    url='https://github.com/adoptium/temurin21-binaries/releases/download/jdk-21.0.12.1%2B1/OpenJDK21U-jdk_x64_windows_hotspot_21.0.12.1_1.zip',
    sha256='f9d6e191ab098c0d416e7d588a24420a8621cd2f4720dab2459b8b7b2d2d8b4e',root=None)]

def digest(path):
    h=hashlib.sha256()
    with path.open('rb') as source:
        while data:=source.read(4*1024*1024):h.update(data)
    return h.hexdigest()

def main():
    output=ROOT/'tools/source-analysis';output.mkdir(parents=True,exist_ok=True);records=[]
    for package in PACKAGES:
        archive=output/package['archive']
        if not archive.exists() or digest(archive)!=package['sha256']:
            request=urllib.request.Request(package['url'],headers={'User-Agent':'wa2-source-analysis'})
            temporary=archive.with_suffix('.zip.part');total=0;next_progress=64*1024*1024
            with urllib.request.urlopen(request,timeout=60) as response,temporary.open('wb') as target:
                while data:=response.read(4*1024*1024):
                    target.write(data);total+=len(data)
                    if total>800*1024*1024:raise ValueError('Unexpected analysis dependency size')
                    if total>=next_progress:print(package['name'],total,'bytes downloaded',flush=True);next_progress+=64*1024*1024
            if digest(temporary)!=package['sha256']:raise ValueError('Analysis dependency checksum mismatch: '+package['name'])
            temporary.replace(archive)
        with zipfile.ZipFile(archive) as source:
            names=source.namelist();roots={name.split('/')[0] for name in names}
            assert len(roots)==1;directory=next(iter(roots))
            if package['root'] is not None:assert directory==package['root']
            complete=output/(directory+'.verified.json')
            if not complete.exists() or json.loads(complete.read_text()).get('archive_sha256')!=package['sha256']:
                for item in source.infolist():
                    destination=(output/item.filename).resolve()
                    if not destination.is_relative_to(output.resolve()):raise ValueError('Analysis archive path escaped tools directory')
                    if item.is_dir():destination.mkdir(parents=True,exist_ok=True);continue
                    destination.parent.mkdir(parents=True,exist_ok=True)
                    with source.open(item) as incoming,destination.open('wb') as outgoing:shutil.copyfileobj(incoming,outgoing)
                complete.write_text(json.dumps(dict(archive_sha256=package['sha256'],entries=len(names)),indent=2)+'\n')
        records.append(dict(**package,path=str(output/directory),archive_bytes=archive.stat().st_size))
        print(package['name'],'verified and extracted',flush=True)
    (output/'dependencies.json').write_text(json.dumps(dict(schema='wa2.source-analysis-dependencies.v1',packages=records,game_runtime_dependency=False),indent=2)+'\n')

if __name__=='__main__':main()
