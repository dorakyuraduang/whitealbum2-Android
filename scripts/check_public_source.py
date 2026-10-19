"""Audit public Git paths without opening ignored game files or private caches."""
import argparse
import json
from pathlib import Path, PurePosixPath
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BLOCKED_ROOTS = {'build', 'out', 'logs', 'res', 'Wa2Res', 'generated', '.codex', '.agents'}
BLOCKED_SUFFIXES = {'.exe', '.pak', '.apk', '.aab', '.dll', '.so', '.dylib', '.a', '.lib', '.obj', '.o', '.bin', '.p12', '.pfx', '.jks', '.keystore', '.key', '.pem', '.sav', '.save', '.dmp'}
SECRETS = [re.compile(rb'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----'),
           re.compile(rb'\bgh[pousr]_[A-Za-z0-9]{32,255}\b'),
           re.compile(rb'\bgithub_pat_[A-Za-z0-9_]{40,255}\b'),
           re.compile(rb'\bAKIA[0-9A-Z]{16}\b')]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prospective', action='store_true', help='Also inspect nonignored untracked source paths')
    args = parser.parse_args()
    command = ['git', '-c', 'safe.directory=' + ROOT.as_posix(), 'ls-files', '-z', '--cached']
    if args.prospective: command += ['--others', '--exclude-standard']
    result = subprocess.run(command, cwd=ROOT, capture_output=True, check=True)
    paths = sorted(set(name.decode('utf-8') for name in result.stdout.split(b'\0') if name))
    violations = []
    total = 0
    for name in paths:
        relative = PurePosixPath(name)
        # Decide from the path BEFORE opening or hashing any candidate.
        if relative.parts[0] in BLOCKED_ROOTS or relative.suffix.lower() in BLOCKED_SUFFIXES or relative.name.startswith('.env') or relative.name in ('hosts.yml', 'credentials', 'local.properties'):
            violations.append({'file': name, 'reason': 'private/binary/game input path'}); continue
        path = ROOT / name
        if path.is_symlink() or not path.resolve().is_relative_to(ROOT):
            violations.append({'file': name, 'reason': 'symlink or workspace escape'}); continue
        size = path.stat().st_size
        if size > 50 * 1024 * 1024:
            violations.append({'file': name, 'reason': 'source file exceeds publication limit'}); continue
        data = path.read_bytes(); total += size
        if any(pattern.search(data) for pattern in SECRETS):
            violations.append({'file': name, 'reason': 'possible credential; value suppressed'})
    print(json.dumps(dict(files=len(paths), bytes=total, private_files_opened=False, violations=violations, passed=not violations), indent=2))
    if violations: raise SystemExit(1)


if __name__ == '__main__': main()
