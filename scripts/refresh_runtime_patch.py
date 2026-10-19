"""Regenerate the local runtime patch against its bundled upstream archive."""
from pathlib import Path, PurePosixPath
import difflib
import hashlib
import json
import re
import subprocess
import uuid
import zipfile

ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/'tools/WindowsRecompTool-main'
ARCHIVE=ROOT/'tools/winrecomp.zip'
PATCH=ROOT/'patches/winrecomp-wa2.patch'


def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    originals={}
    with zipfile.ZipFile(ARCHIVE) as archive:
        for info in archive.infolist():
            if info.is_dir():continue
            parts=PurePosixPath(info.filename).parts
            if len(parts)<2 or '..' in parts:raise ValueError('Invalid upstream path')
            name=PurePosixPath(*parts[1:]).as_posix()
            originals[name]=archive.read(info)
    names=set(originals)
    # Retain all previous patch additions, including non-source fixtures.
    names.update(re.findall(r'^\+\+\+ b/(.+)$',PATCH.read_text(encoding='utf-8'),re.M))
    for directory in ('include','src','tests','tools'):
        for path in (SOURCE/directory).rglob('*'):
            if path.is_file() and path.suffix.lower() in ('.cpp','.hpp','.c','.h','.s','.asm','.py','.cmake'):
                if '__pycache__' not in path.parts:names.add(path.relative_to(SOURCE).as_posix())
    changes=[];patch=[]
    for name in sorted(names):
        path=SOURCE/name
        if not path.resolve().is_relative_to(SOURCE.resolve()):raise ValueError('Source path escapes checkout')
        before=originals.get(name);after=path.read_bytes() if path.is_file() else None
        if before==after:continue
        try:
            old=(before or b'').decode('utf-8').replace('\r\n','\n')
            new=(after or b'').decode('utf-8').replace('\r\n','\n')
        except UnicodeDecodeError as error:
            raise ValueError('Changed binary needs an explicit binary patch: '+name) from error
        if old==new:continue
        delta=difflib.unified_diff(old.splitlines(True),new.splitlines(True),
            fromfile='a/'+name if before is not None else '/dev/null',
            tofile='b/'+name if after is not None else '/dev/null')
        for line in delta:
            patch.append(line if line.endswith('\n') else line+'\n\\ No newline at end of file\n')
        changes.append(name)
    temporary=PATCH.with_suffix('.patch.tmp');temporary.write_text(''.join(patch),encoding='utf-8',newline='\n')
    temporary.replace(PATCH)
    checkout=ROOT/'build/runtime-patch-check'/uuid.uuid4().hex;checkout.mkdir(parents=True)
    for name,data in originals.items():
        target=checkout/name
        if not target.resolve().is_relative_to(checkout.resolve()):raise ValueError('Invalid checkout path')
        target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
    subprocess.run(['git','apply','--check',str(PATCH)],cwd=checkout,check=True)
    subprocess.run(['git','apply',str(PATCH)],cwd=checkout,check=True)
    for name in changes:
        expected=SOURCE/name;actual=checkout/name
        if expected.exists()!=actual.exists():raise ValueError('Patch did not reproduce '+name)
        if expected.exists() and expected.read_text(encoding='utf-8')!=actual.read_text(encoding='utf-8'):raise ValueError('Patched source differs: '+name)
    evidence=dict(schema='wa2.runtime-patch.v1',archive=str(ARCHIVE),archive_sha256=digest(ARCHIVE),
        patch=str(PATCH),patch_sha256=digest(PATCH),files=changes,
        source_sha256={name:digest(SOURCE/name) if (SOURCE/name).exists() else None for name in changes},
        verification_checkout=str(checkout),git_apply_check=True,normalized_sources_match=True)
    (ROOT/'logs/runtime-patch.json').write_text(json.dumps(evidence,indent=2)+'\n')
    print(json.dumps(dict(files=len(changes),patch_sha256=evidence['patch_sha256'])))


if __name__=='__main__':main()
