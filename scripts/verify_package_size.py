"""Bind physical package-size changes to matching EXE/PDB and link inputs."""
from pathlib import Path
import hashlib
import json
import sys
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/python'))
import pefile
from inspect_binary_size import inspect


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def read(path):
    return json.loads(Path(path).read_text())


def bound(path):
    return dict(file=str(Path(path).resolve()),sha256=digest(path))


def groups(report):
    totals={}
    for row in report['modules']:
        module,archive=row['module'].replace('\\','/').rsplit('/',1)[-1],row['archive']
        if module=='native_linear.S.obj':group='translated_assembly_and_tables'
        elif module=='compact_ops.cpp.obj' or 'code_page_' in module:group='translated_cpp_fallback'
        elif module=='original_code.S.obj':group='embedded_original_exe'
        elif module=='x87_helpers.S.obj':group='floating_point_helpers'
        elif 'wa2_engine' in archive or 'wa2_platform' in archive or 'wa2_sdl_audio' in archive:group='ordinary_engine_and_platform'
        elif 'wa2_port.a' in archive or module=='fp_sequences_release.cpp.obj':group='adapters_and_old_hot_kernels'
        elif 'winrecomp_guest' in archive:group='windows_runtime'
        elif 'libSDL2' in archive:group='sdl2'
        else:group='other_libraries_and_linker'
        totals[group]=totals.get(group,0)+row['bytes']
    return totals


def verification():
    before_path=ROOT/'logs/codec-binary-size-before.json'; after_path=ROOT/'logs/codec-binary-size-after.json'
    before,after=read(before_path),read(after_path)
    for report in [before,after]:
        program,pdb=report['program'],report['pdb']
        assert digest(program['file'])==program['sha256'] and digest(pdb['file'])==pdb['sha256']
        assert inspect(Path(program['file']),Path(pdb['file']))==report
    output=ROOT/'out/WA2_SDL2_x64.exe';value=digest(output)
    assert value==after['program']['sha256']==digest(ROOT/'res'/output.name)
    link_path=ROOT/'build/wa2-win64/candidate/WA2_SDL2_x64.link.json';link=read(link_path)
    assert link['output_sha256']==value and link['coff_opt_ref'] and not link['shell']
    assert digest(link['pdb'])==link['pdb_sha256'] and digest(link['compiler'])==link['compiler_sha256']
    assert link['uncollected_bytes']-link['output_bytes']==link['bytes_removed']>0
    assert any(arg.lower()=='-opt:ref' for arg in link['linker_argv'])
    old,new=groups(before),groups(after)
    assert new['floating_point_helpers']<=old['floating_point_helpers']<100_000
    assert old['translated_assembly_and_tables']>new['translated_assembly_and_tables']
    assert old['translated_cpp_fallback']>new['translated_cpp_fallback']
    assert old['embedded_original_exe']==new['embedded_original_exe']==(ROOT/'WA2_chs.exe').stat().st_size
    old_pe,new_pe=pefile.PE(before['program']['file']),pefile.PE(str(output))
    imports=lambda image:{item.dll.decode().lower() for item in image.DIRECTORY_ENTRY_IMPORT}
    assert imports(new_pe)<=imports(old_pe)
    compact=read(ROOT/'build/wa2-win64/compact-fallback/manifest.json')
    assert compact['instructions']==139987 and compact['every_body_expanded_and_matched']
    from verify_retired_modules import verify as check_retired
    retired=check_retired();assert retired==read(ROOT/'logs/retired-modules-verification.json')
    assert compact['release_excluded_instructions']==retired['removed_cpp_instructions']>19000
    objects=[arg.replace('\\','/').lower() for arg in link['linker_argv'] if 'code_page_' in arg]
    assert len(objects)==compact['pages'] and all('/compact-fallback/release/' in arg for arg in objects)
    assert not any('codec_oracle' in arg.lower() for arg in link['linker_argv'])
    names={row['module'].replace('\\','/').rsplit('/',1)[-1] for row in after['modules']}
    assert not names.intersection(('vorbis_batch.S.obj','vorbis_batch.cpp.obj','vorbis_butterfly.S.obj','vorbis_butterfly.cpp.obj','legacy_vorbis_kernels.cpp.obj','fp_sequences.cpp.obj'))
    assert 'fp_sequences_release.cpp.obj' in names
    sources=['scripts/inspect_binary_size.py','scripts/link_release_pe.py','scripts/verify_package_size.py',
             'tools/WindowsRecompTool-main/tools/generate_fp_asm.py','port/CMakeLists.txt',
             'port/retired_modules.json','port/compact_fallback.cmake','scripts/compact_fallback.py','scripts/verify_retired_modules.py']
    return dict(schema='wa2.package-size-verification.v1',output_sha256=value,
                before=bound(before_path),after=bound(after_path),release_link=bound(link_path),
                before_bytes=before['program']['bytes'],after_bytes=after['program']['bytes'],
                bytes_removed=before['program']['bytes']-after['program']['bytes'],
                before_groups=old,after_groups=new,original_exe_bytes=(ROOT/'WA2_chs.exe').stat().st_size,
                size_ratio_to_original=output.stat().st_size/(ROOT/'WA2_chs.exe').stat().st_size,
                new_external_dlls=[],executable_packer_used=False,unreplaced_instruction_positions_removed=False,
                retired_library_instruction_implementations=retired['removed_cpp_instructions'],
                retired_library=bound(ROOT/'logs/retired-modules-verification.json'),test_oracle_pages_linked_into_release=False,
                complete_native_reconstruction=False,source_files=[bound(ROOT/name) for name in sources],
                screen_access=False,passed=True)


if __name__=='__main__':
    proof=verification();(ROOT/'logs/package-size-verification.json').write_text(json.dumps(proof,indent=2)+'\n')
    print(json.dumps({key:proof[key] for key in ['before_bytes','after_bytes','bytes_removed','size_ratio_to_original','complete_native_reconstruction','passed']},indent=2))
