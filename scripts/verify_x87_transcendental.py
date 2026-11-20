"""Verify scalar x87 software arithmetic against finite native instructions.

No game, emulator or source executable is run. The hardware oracle is linked
only into this Windows memory fixture, never the production libraries.
"""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import subprocess
from prepare_special_contents import ROOT,digest,write_json

RECEIPT=ROOT/'logs/x87-transcendental-verification.json'
REPORT=ROOT/'logs/x87-transcendental-check.json'
BUILD=ROOT/'build/x87-transcendental-check'

def bindings():
    files=[p for p in (ROOT/'third_party/x87_math').rglob('*') if p.is_file()]
    files += [ROOT/name for name in ('recompiled/transcend.cpp','recompiled/extended80.cpp',
        'recompiled/extended80.hpp','recompiled/x87_kernel.hpp','recompiled/fp_registers.hpp',
        'recompiled/fp_execution.hpp','tests/transcendental.cpp','tests/transcendental/CMakeLists.txt',
        'scripts/verify_x87_transcendental.py')]
    return {p.relative_to(ROOT).as_posix():digest(p) for p in sorted(files)}

def fresh():
    proof=json.loads(RECEIPT.read_text())
    if not proof['passed'] or proof['source_sha256']!=bindings():raise ValueError('Stale x87 software arithmetic verification')
    for key in ('report','binary'):
        if digest(Path(proof[key]['file']))!=proof[key]['sha256']:raise ValueError('Changed x87 arithmetic check '+key)
    return proof

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--check',action='store_true');args=parser.parse_args()
    if args.check:fresh();print('x87 scalar verification is current.');return
    upstream=ROOT/'third_party/x87_math';meta=json.loads((upstream/'UPSTREAM.json').read_text())
    for row in meta['files']:
        if digest(upstream/Path(row['path']).relative_to('bochs/cpu'))!=row['sha256']:
            raise ValueError('Changed pinned arithmetic source: '+row['path'])
    env=os.environ.copy();env.update(CMAKE_BUILD_PARALLEL_LEVEL='1',ZIG_GLOBAL_CACHE_DIR=str(ROOT/'build/zig-cache'),
        ZIG_LOCAL_CACHE_DIR=str(ROOT/'build/zig-local'),TEMP=str(ROOT/'build/temp'),TMP=str(ROOT/'build/temp'))
    flags=subprocess.CREATE_NO_WINDOW|subprocess.BELOW_NORMAL_PRIORITY_CLASS
    cmake=ROOT/'tools/python/cmake/data/bin/cmake.exe'
    with (ROOT/'logs/x87-transcendental-build.log').open('wb') as log:
        def run(command,timeout=300):subprocess.run(list(map(str,command)),cwd=ROOT,env=env,creationflags=flags,
            stdout=log,stderr=subprocess.STDOUT,check=True,timeout=timeout)
        run([cmake,'-S',ROOT/'tests/transcendental','-B',BUILD,'-G','Ninja',
            '-DCMAKE_TOOLCHAIN_FILE='+str(ROOT/'scripts/zig-toolchain.cmake'),'-DCMAKE_BUILD_TYPE=Release'])
        run([cmake,'--build',BUILD,'--target','wa2_transcendental_tests','--parallel','1'])
        binary=BUILD/'wa2_transcendental_tests.exe';run([binary,REPORT],60)
    report=json.loads(REPORT.read_text())
    if not report['passed'] or report['cases']<107520 or not report['register_file_agreement']:
        raise ValueError('Incomplete x87 software arithmetic checks')
    proof=dict(schema='wa2.x87-transcendental-verification.v1',passed=True,source_sha256=bindings(),
        upstream_revision=meta['revision'],cases=report['cases'],mismatches=report['mismatches'],
        report=dict(file=str(REPORT),sha256=digest(REPORT)),binary=dict(file=str(binary),sha256=digest(binary)),
        game_launched=False,screen_access=False,source_executable_opened=False)
    write_json(RECEIPT,proof);print('PASS',report['cases'],'strict x87 hardware/register-file cases.',flush=True)

if __name__=='__main__':main()
