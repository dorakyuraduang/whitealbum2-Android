"""Save exact graph/patch/license evidence for the local target build."""
from pathlib import Path
import difflib
import json
import subprocess
import sys
import zipfile

ROOT=Path(__file__).resolve().parent.parent

def main():
    profile=json.loads((ROOT/'config'/'wa2.json').read_text())
    args=[str(ROOT/'build'/'winrecomp'/'winrecomp.exe'),'cfg',str(ROOT/'WA2_chs.exe'),str(ROOT/'logs'/'cfg.json')]
    for seed in profile['seeds']:args+=['--seed',hex(seed)]
    subprocess.run(args,check=True)
    graph=json.loads((ROOT/'logs'/'cfg.json').read_text())
    if graph['diagnostics'] or graph['budget_exhausted']:raise RuntimeError('Final graph has discovery errors')
    source=ROOT/'tools'/'WindowsRecompTool-main';patch=[]
    with zipfile.ZipFile(ROOT/'tools'/'winrecomp.zip') as archive:
        prefix=archive.namelist()[0].split('/')[0]+'/'
        originals={name[len(prefix):]:name for name in archive.namelist() if not name.endswith('/')}
        before=archive.read(originals['CMakeLists.txt']).decode('utf-8');after=(source/'CMakeLists.txt').read_text(encoding='utf-8')
        if before!=after:patch.extend(difflib.unified_diff(before.splitlines(keepends=True),after.splitlines(keepends=True),fromfile='a/CMakeLists.txt',tofile='b/CMakeLists.txt'))
        for folder in ['include','src','tools','tests']:
            for path in sorted((source/folder).rglob('*')):
                if not path.is_file() or path.suffix not in ('.hpp','.cpp','.inc','.py'):continue
                relative=path.relative_to(source).as_posix()
                before=archive.read(originals[relative]).decode('utf-8') if relative in originals else ''
                after=path.read_text(encoding='utf-8')
                if before==after:continue
                patch.extend(difflib.unified_diff(before.splitlines(keepends=True),after.splitlines(keepends=True),fromfile='a/'+relative if before else '/dev/null',tofile='b/'+relative))
        version={'upstream':'https://github.com/Zippingrocks/WindowsRecompTool','commit':archive.comment.decode(),'local_patch':'patches/winrecomp-wa2.patch'}
    patch_dir=ROOT/'patches';patch_dir.mkdir(exist_ok=True)
    (patch_dir/'winrecomp-wa2.patch').write_text(''.join(patch),encoding='utf-8',newline='\n')
    (patch_dir/'upstream.json').write_text(json.dumps(version,indent=2)+'\n')
    licenses=ROOT/'out'/'licenses';licenses.mkdir(exist_ok=True)
    for label,path in [('WinRecomp',source/'LICENSE'),('Zydis',source/'third_party'/'zydis'/'LICENSE'),('Zycore',source/'third_party'/'zydis'/'dependencies'/'zycore'/'LICENSE')]:
        (licenses/(label+'.txt')).write_bytes(path.read_bytes())
    for label,path in [('SDL2',ROOT/'tools/SDL-release-2.32.10/LICENSE.txt'),('MojoShader',ROOT/'tools/mojoshader-main/LICENSE.txt'),('MojoShaderAssembler',ROOT/'tools/mojoshader_assembler-main/LICENSE.txt')]:
        (licenses/(label+'.txt')).write_bytes(path.read_bytes())
    subprocess.run([sys.executable,str(ROOT/'scripts'/'verify_output.py')],check=True)
    print(json.dumps({'instructions':len(graph['instructions']),'blocks':len(graph['blocks']),'functions':len(graph['function_candidates']),'extra_entry_seeds':len(profile['seeds']),'diagnostics':graph['diagnostics']},indent=2))

if __name__=='__main__':main()
