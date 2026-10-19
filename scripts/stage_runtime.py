"""Keep the resource-directory EXE identical to the built native executable."""
from pathlib import Path
import hashlib
import argparse
import json
import os
import shutil
import struct
import tempfile

ROOT=Path(__file__).resolve().parent.parent
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--candidate',type=Path);args=parser.parse_args()
    source=ROOT/'out/WA2_SDL2_x64.exe';destination=ROOT/'res/WA2_SDL2_x64.exe'
    if not destination.parent.is_dir():return
    if args.candidate:
        candidate=args.candidate.resolve()
        if not candidate.is_relative_to(ROOT):raise ValueError('Candidate is outside the workspace')
        binary=candidate.read_bytes();pe=struct.unpack_from('<I',binary,0x3c)[0]
        if binary[:2]!=b'MZ' or binary[pe:pe+4]!=b'PE\0\0' or struct.unpack_from('<H',binary,pe+4)[0]!=0x8664 or struct.unpack_from('<H',binary,pe+24)[0]!=0x20b:
            raise ValueError('Candidate is not the AMD64 PE32+ executable')
        from verify_native_generation import check
        check()
        from verify_retired_modules import verify as check_retired
        if check_retired()!=json.loads((ROOT/'logs/retired-modules-verification.json').read_text()):
            raise ValueError('Retired module boundaries or source inventories changed')
        expected=digest(candidate);suite=json.loads((ROOT/'logs/execution-suite.json').read_text())
        if suite['output_sha256']!=expected or len(suite['fixtures'])!=28 or suite['core_tests']!=10 or suite['screen_access']:
            raise ValueError('Candidate has not passed the complete non-screen execution suite')
        if suite['retired_module_proof_sha256']!=digest(ROOT/'logs/retired-modules-verification.json'):
            raise ValueError('Retired module proof changed after execution tests')
        directory=ROOT/'build/wa2-win64/wa2_port'
        for field,name in [('port_library_sha256','libwa2_port.a'),('compact_library_sha256','libwa2_compact_ops.a'),
                ('codec_oracle_library_sha256','libwa2_codec_oracle.a'),
                ('native_engine_library_sha256','engine/libwa2_engine.a'),('platform_library_sha256','libwa2_platform.a'),('sdl_audio_library_sha256','libwa2_sdl_audio.a'),('sdl_library_sha256','sdl2/libSDL2.a')]:
            if digest(directory/name)!=suite[field]:raise ValueError('Candidate test libraries changed: '+name)
        for fixture in suite['fixtures']:
            if fixture['return_code'] or digest(Path(fixture['log']))!=fixture['log_sha256']:raise ValueError('Candidate regression evidence changed')
            if fixture.get('report') and digest(Path(fixture['report']))!=fixture['report_sha256']:raise ValueError('Candidate regression report changed')
            program=directory/('engine' if fixture['fixture']=='wa2_pcm_audio_tests' else '')/(fixture['fixture']+'.exe')
            if digest(program)!=fixture['program_sha256']:raise ValueError('Candidate test program changed')
        if digest(ROOT/'build/wa2-win64/winrecomp/libwinrecomp_guest.a')!=suite['runtime_library_sha256'] or digest(Path(suite['core_log']))!=suite['core_log_sha256']:
            raise ValueError('Candidate core runtime/test evidence changed')
        if digest(ROOT/'build/wa2-win64/winrecomp/libwinrecomp_native.a')!=suite['native_helpers_library_sha256']:
            raise ValueError('Candidate floating-point helper library changed')
        from verify_fault_table import verify
        if verify()!=json.loads((ROOT/'logs/compact-fault-table-tests.json').read_text()):
            raise ValueError('Candidate recovery table has not passed compiled-address validation')
        if source.exists():
            prior=digest(source);backup=ROOT/'build/runtime-backups'/prior/source.name;backup.parent.mkdir(parents=True,exist_ok=True)
            if not backup.exists():shutil.copy2(source,backup)
        descriptor,temporary=tempfile.mkstemp(prefix='wa2-publish-',suffix='.exe',dir=source.parent);os.close(descriptor)
        try:
            shutil.copy2(candidate,temporary)
            if digest(Path(temporary))!=expected:raise RuntimeError('Candidate publication checksum mismatch')
            os.replace(temporary,source)
        finally:
            if Path(temporary).exists():Path(temporary).unlink()
        symbols=candidate.with_suffix('.pdb')
        if symbols.exists():
            target=ROOT/'build/symbols'/symbols.name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(symbols,target)
    expected=digest(source);previous=digest(destination) if destination.exists() else None
    if previous!=expected:
        if previous:
            backup=ROOT/'build/runtime-backups'/previous/destination.name;backup.parent.mkdir(parents=True,exist_ok=True)
            if not backup.exists():shutil.copy2(destination,backup)
        descriptor,temporary=tempfile.mkstemp(prefix='wa2-stage-',suffix='.exe',dir=destination.parent);os.close(descriptor)
        try:
            shutil.copy2(source,temporary)
            if digest(Path(temporary))!=expected:raise RuntimeError('Staged EXE checksum mismatch')
            os.replace(temporary,destination)
        finally:
            if Path(temporary).exists():Path(temporary).unlink()
    proof={'schema':'wa2.runtime-staging.v1','output_sha256':expected,'resource_executable':str(destination),
           'resource_executable_sha256':digest(destination),'previous_sha256':previous,'matches_build':digest(destination)==expected}
    # Preserve development artifacts outside the runnable output directory.
    moved=[]
    for name in ('WA2_SDL2_x64.pdb','WA2_x64.exe','WA2_x64.pdb'):
        artifact=ROOT/'out'/name
        if not artifact.exists():continue
        value=digest(artifact)
        target=(ROOT/'build/symbols'/name if name=='WA2_SDL2_x64.pdb' else ROOT/'build/legacy-output'/value/name)
        if not artifact.resolve().is_relative_to(ROOT) or not target.resolve().is_relative_to(ROOT):
            raise RuntimeError('Artifact staging path escaped the workspace')
        target.parent.mkdir(parents=True,exist_ok=True)
        os.replace(artifact,target)
        moved.append(dict(file=str(target),sha256=value))
    proof['development_artifacts']=moved
    (ROOT/'logs/runtime-staging.json').write_text(json.dumps(proof,indent=2)+'\n')
    print(json.dumps(proof,indent=2))

if __name__=='__main__':main()
