"""Run verified local Ghidra headlessly; export source-review material only."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import uuid
import argparse
import re
ROOT=Path(__file__).resolve().parents[1]

def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--prepare-types',action='store_true');options=parser.parse_args()
    dependency_file=ROOT/'tools/source-analysis/dependencies.json';dependencies=json.loads(dependency_file.read_text())
    packages={p['name']:p for p in dependencies['packages']}
    ghidra=Path(packages['ghidra']['path']);jdk=Path(packages['jdk']['path'])
    original=ROOT/'WA2_chs.exe';sha=digest(original)
    if sha!='246a263b965147a274fa59cdd7a72f6be10ba063d8cf4e61e74baefb56f29327':raise ValueError('Analysis image mismatch')
    work=ROOT/'build/source-analysis';projects=work/'projects';output=work/('functions-typed' if options.prepare_types else 'functions')
    for path in [work,projects,output,work/'settings',work/'cache',work/'temp']:
        if not path.resolve().is_relative_to(ROOT):raise ValueError('Analysis path escaped workspace')
        path.mkdir(parents=True,exist_ok=True)
    project='wa2-'+sha[:16]+('-typed' if options.prepare_types else '');run=uuid.uuid4().hex
    if options.prepare_types:
        table=ROOT/'port/d3d9_tables.inc';source=ROOT/'port/direct3d_sdl.cpp'
        methods={kind:[dict(name=name,arguments=int(count)) for name,count in re.findall(r'\{"([^"]+)",(\d+)\}',body)]
            for kind,body in re.findall(r'static constexpr Method methods_(\w+)\[\] = \{(.*?)\n\};',table.read_text(),re.S)}
        imports=[dict(dll=dll,name=name,arguments=int(count)) for dll,name,count in re.findall(r'api\(p,"([^"]+)","([^"]+)",(\d+),',source.read_text())]
        assert len(methods['device'])==119 and len(methods['texture'])==22
        (work/'abi-signatures.json').write_text(json.dumps(dict(methods=methods,imports=imports,
            source_sha256={str(table.relative_to(ROOT)):digest(table),str(source.relative_to(ROOT)):digest(source)}),indent=2)+'\n')
    environment=os.environ.copy();environment.update(JAVA_HOME=str(jdk),GHIDRA_HEADLESS_MAXMEM='2G',
        GHIDRA_HEADLESS_JAVA_OPTIONS=' '.join(['-Djava.awt.headless=true','-Dapplication.settingsdir='+str(work/'settings'),
            '-Dapplication.cachedir='+str(work/'cache'),'-Dapplication.tempdir='+str(work/'temp'),'-Djava.io.tmpdir='+str(work/'temp')]),
        TEMP=str(work/'temp'),TMP=str(work/'temp'))
    command=['cmd.exe','/d','/c',str(ghidra/'support/analyzeHeadless.bat'),str(projects),project]
    if (projects/(project+'.gpr')).exists():command+=['-process',original.name,'-noanalysis']
    else:command+=['-import',str(original),'-analysisTimeoutPerFile','300']
    command+=['-max-cpu','2','-scriptPath',str(ROOT/'scripts/analysis')]
    if options.prepare_types:command+=['-preScript','PrepareEngineTypes.java',str(work/'abi-signatures.json')]
    command+=['-postScript','ExportEngineFunctions.java',str(output),sha,run,
        '-log',str(ROOT/'logs/engine-source-analysis.log'),'-scriptlog',str(ROOT/'logs/engine-source-export.log')]
    log=ROOT/'logs/engine-source-console.log'
    with log.open('wb') as console:
        result=subprocess.run(command,cwd=ROOT,env=environment,stdout=console,stderr=subprocess.STDOUT,
            creationflags=subprocess.CREATE_NO_WINDOW,timeout=1800)
    if result.returncode:raise RuntimeError(log.read_text(errors='replace')[-6000:])
    if digest(original)!=sha:raise ValueError('Original image changed during analysis')
    manifest=output/'manifest.json';data=json.loads(manifest.read_text())
    assert data['run_id']==run and data['input_sha256']==sha and data['complete'] and data['pseudocode_only']
    for row in data['functions']:
        if row.get('c_file'):assert digest(output/row['c_file'])==row['c_sha256']
    proof=dict(schema='wa2.engine-source-analysis-run.v1',input_sha256=sha,command=command,
        manifest=dict(file=str(manifest),sha256=digest(manifest)),functions=len(data['functions']),
        decompiled=sum('c_file' in f for f in data['functions']),failed=sum('error' in f for f in data['functions']),
        dependencies=dict(file=str(dependency_file),sha256=digest(dependency_file)),
        script_sha256=digest(ROOT/'scripts/analysis/ExportEngineFunctions.java'),pseudocode_only=True,
        prepared_types=options.prepare_types,
        compiled_into_game=False,correctness_verified=False,screen_access=False)
    (ROOT/'logs/engine-source-analysis-run.json').write_text(json.dumps(proof,indent=2)+'\n')
    print(json.dumps({k:proof[k] for k in ['functions','decompiled','failed','pseudocode_only','compiled_into_game']},indent=2))

if __name__=='__main__':main()
