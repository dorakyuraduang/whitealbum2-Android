"""Refresh the same static program with a tested compiler, keeping a backup."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
import uuid

ROOT = Path(__file__).resolve().parent.parent
GUARD = re.compile(r'case (0x[0-9a-f]+)u: \{ wr::check_code\(m,s.eip,\{([0-9,]+)\}\);')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--tool', type=Path, required=True)
    args = parser.parse_args()
    destination = (ROOT/'generated/wa2-win64').resolve()
    stage_root = (ROOT/'build/generation-stage'/uuid.uuid4().hex).resolve()
    if not destination.is_relative_to(ROOT) or not stage_root.is_relative_to(ROOT):
        raise ValueError('Generation paths leave the workspace')
    stage = stage_root/'project'
    backup = stage_root/'previous'
    stage_root.mkdir(parents=True)
    subprocess.run([sys.executable, str(ROOT/'scripts/generate_project.py'), '--tool',
                    str(args.tool.resolve()), '--out', str(stage)], cwd=ROOT, check=True)
    before = json.loads((destination/'manifest.json').read_text())
    after = json.loads((stage/'manifest.json').read_text())
    if before != after:
        raise ValueError('Refreshing compiler templates changed the program manifest')
    pages = sorted(destination.glob('code_page_*.cpp'))
    if {p.name for p in pages} != {p.name for p in stage.glob('code_page_*.cpp')}:
        raise ValueError('Refreshing compiler templates changed the translated page set')
    for previous in pages:
        if GUARD.findall(previous.read_text()) != GUARD.findall((stage/previous.name).read_text()):
            raise ValueError('Translated instruction addresses or byte guards changed: '+previous.name)
    updates = []
    backup.mkdir()
    for source in stage.iterdir():
        target = destination/source.name
        if target.exists() and target.read_bytes() == source.read_bytes():
            continue
        old_hash = digest(target) if target.exists() else None
        if target.exists():
            shutil.copy2(target, backup/target.name)
        shutil.copy2(source, target)
        updates.append(dict(file=target.name, before_sha256=old_hash, after_sha256=digest(target)))
    proof = dict(schema='wa2.compiler-refresh.v1', compiler=str(args.tool.resolve()),
                 compiler_sha256=digest(args.tool), backup=str(backup),
                 manifest_unchanged=True, instruction_byte_guards_unchanged=True, updates=updates)
    (ROOT/'logs/compiler-refresh.json').write_text(json.dumps(proof, indent=2)+'\n')
    print(json.dumps(dict(updated_files=len(updates), backup=str(backup),
                          instructions=after['instructions']), indent=2))


if __name__ == '__main__':
    main()
