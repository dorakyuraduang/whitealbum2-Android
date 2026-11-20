"""Build/verify the common Special program without starting a game or device UI."""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import sys

from prepare_special_contents import ROOT, digest, write_json

RECEIPT = ROOT/'logs/special-aot-verification.json'


def source_bindings():
    files = set()
    for directory in ('recompiled', 'platform', 'engine', 'hosts', 'android/native',
                      'tools/WindowsRecompTool-main/include', 'tools/WindowsRecompTool-main/src/runtime',
                      'generated/special-contents/aot'):
        files.update(p for p in (ROOT/directory).rglob('*') if p.is_file() and p.suffix in
                     {'.cpp','.hpp','.h','.inc','.cmake','.txt'})
    files.update(ROOT/name for name in ('config/porting_contract.json', 'config/special_contents.json',
        'scripts/generate_shared_aot.py','scripts/generate_special_aot.py','scripts/prepare_special_contents.py',
        'scripts/verify_special_contents.py','tests/special_aot.cpp','tests/special_runtime.cpp',
        'generated/special-contents/preparation.json','generated/special-contents/aot/manifest.json',
        'generated/special-contents/callback-upgrade/manifest.json','scripts/audit_special_callbacks.py',
        'scripts/analysis/ExportCachedListing.java',
        'tools/WindowsRecompTool-main/src/pe.cpp','tools/WindowsRecompTool-main/src/resources.cpp'))
    return {p.relative_to(ROOT).as_posix(): digest(p) for p in sorted(files)}


def compiled_special(directory):
    entries = json.loads((directory/'compile_commands.json').read_text())
    prefix = (ROOT/'generated/special-contents/aot').resolve()
    return {Path(item['file']).resolve().relative_to(ROOT).as_posix(): digest(Path(item['file']))
            for item in entries if Path(item['file']).resolve().is_relative_to(prefix)
            and Path(item['file']).name != 'probe_oracle.cpp'}


def fresh():
    proof = json.loads(RECEIPT.read_text())
    if not proof['passed'] or proof['input_sha256'] != json.loads((ROOT/'config/special_contents.json').read_text())['sha256']:
        raise ValueError('Missing or wrong Special verification')
    for name, sha in proof['source_sha256'].items():
        if digest(ROOT/name) != sha:
            raise ValueError('Stale Special source verification: '+name)
    for artifact in proof['artifacts'].values():
        if digest(Path(artifact['file'])) != artifact['sha256']:
            raise ValueError('Special artifact changed after verification')
    for report in proof['checks'].values():
        if digest(Path(report['file'])) != report['sha256'] or not json.loads(Path(report['file']).read_text())['passed']:
            raise ValueError('Special memory check changed')
        if digest(Path(report['binary'])) != report['binary_sha256']:
            raise ValueError('Special memory check binary changed')
    return proof


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--record-only', action='store_true', help='Verify already-completed serial builds and memory runs')
    parser.add_argument('--check', action='store_true', help='Only validate an existing source-bound receipt')
    args = parser.parse_args()
    if args.check:
        proof=fresh();print('Special source-bound verification is current. Device gameplay remains pending.');return
    flags = getattr(subprocess,'CREATE_NO_WINDOW',0) | getattr(subprocess,'BELOW_NORMAL_PRIORITY_CLASS',0)
    env=os.environ.copy();env.update(CMAKE_BUILD_PARALLEL_LEVEL='1',
        ZIG_GLOBAL_CACHE_DIR=str(ROOT/'build/zig-cache'),ZIG_LOCAL_CACHE_DIR=str(ROOT/'build/zig-local'),
        TEMP=str(ROOT/'build/temp'),TMP=str(ROOT/'build/temp'))
    win=ROOT/'build/shared-host-win64';android=ROOT/'build/shared-aot-android'
    cmake=ROOT/'tools/python/cmake/data/bin/cmake.exe';ndk=ROOT/'tools/android-sdk/ndk/27.0.12077973'
    acmake=ROOT/'tools/android-sdk/cmake/3.22.1/bin/cmake.exe'
    with (ROOT/'logs/special-verification.log').open('wb') as log:
        def run(command,timeout=180):
            subprocess.run(list(map(str,command)),cwd=ROOT,env=env,creationflags=flags,
                stdout=log,stderr=subprocess.STDOUT,check=True,timeout=timeout)
        if not args.record_only:
            run([sys.executable,ROOT/'scripts/audit_special_callbacks.py'],600)
            run([sys.executable,ROOT/'scripts/generate_special_aot.py'])
            run([cmake,'-S',ROOT/'hosts','-B',win,'-G','Ninja',
                '-DCMAKE_TOOLCHAIN_FILE='+str(ROOT/'scripts/zig-toolchain.cmake'),
                '-DCMAKE_BUILD_TYPE=MinSizeRel','-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',
                '-DWA2_SPECIAL_CONTENTS=ON','-DWA2_SPECIAL_CHECKS=ON',
                '-DWA2_SHARED_SDL_ARCHIVE='+str(ROOT/'build/test-content-046/materials/windows/archives/libSDL2.a')])
            run([cmake,'--build',win,'--target','wa2_special_memory_tests','wa2_special_runtime_tests','wa2_special_windows','--parallel','1'],7200)
            for stem in ('memory','runtime'):
                run([win/f'shared_runtime/special/wa2_special_{stem}_tests.exe',ROOT/f'logs/special-{stem}-check.json'],60)
            run([acmake,'-S',ROOT/'tests/shared-aot-android','-B',android,'-G','Ninja',
                '-DCMAKE_MAKE_PROGRAM='+str(ROOT/'tools/python/bin/ninja.exe'),
                '-DCMAKE_TOOLCHAIN_FILE='+str(ndk/'build/cmake/android.toolchain.cmake'),
                '-DANDROID_ABI=arm64-v8a','-DANDROID_PLATFORM=android-26','-DANDROID_STL=c++_shared',
                '-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON','-DCMAKE_BUILD_TYPE=MinSizeRel',
                '-DWA2_AOT_DIAGNOSTICS=OFF','-DWA2_SPECIAL_CONTENTS=ON','-DWA2_SPECIAL_CHECKS=OFF',
                '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON'])
            run([acmake,'--build',android,'--target','wa2_special','--parallel','1'],7200)
        manifest_path=ROOT/'generated/special-contents/aot/manifest.json'
        manifest=json.loads(manifest_path.read_text())
        from audit_special_callbacks import validated_cache
        callbacks=validated_cache()
        for name,sha in manifest['generated_sha256'].items():
            if digest(manifest_path.parent/name)!=sha:raise ValueError('Special generated file changed: '+name)
        windows_sources,android_sources=compiled_special(win),compiled_special(android)
        expected={f'generated/special-contents/aot/{name}':sha for name,sha in manifest['generated_sha256'].items()
                  if name.endswith('.cpp') and name!='probe_oracle.cpp'}
        if windows_sources!=android_sources or windows_sources!=expected:
            raise ValueError('Windows/Android compiled different Special C++ source inventories')
        checks={}
        for stem in ('memory','runtime'):
            path=ROOT/f'logs/special-{stem}-check.json';check=json.loads(path.read_text())
            if not check['passed'] or check['game_launched'] or check['screen_access']:
                raise ValueError('Invalid Special headless verification')
            if stem=='memory' and (check.get('string_regression_cases')!=6 or
                    check.get('reported_rep_address')!=4691986 or not check.get('string_fault_prefix_checked') or
                    not check.get('diagnostic_budget_preserved')):
                raise ValueError('Missing Special REP metering/fault-prefix regression checks')
            if stem=='memory' and (check.get('reported_callback_address')!=4739667 or
                    check.get('recovered_callback_entries_checked')!=len(callbacks['recovered_entries']) or
                    check.get('instruction_starts_checked')!=manifest['instruction_locations']):
                raise ValueError('Missing Special recovered callback catalogue checks')
            if stem=='runtime' and (check.get('reported_callback_address')!=4739667 or
                    not check.get('crt_initializer_registered_filter') or check.get('crt_filter_rejection_cases')!=3 or
                    check.get('game_startup_called') is not False or check.get('bounded_static_callback_entries',0)<4):
                raise ValueError('Missing bounded Special CRT callback regression checks')
            if stem=='memory' and (check.get('reported_copy_address')!=4673248 or check.get('crt_copy_cases',0)<3000 or
                    check.get('recovered_jump_entries_checked')!=len(callbacks['recovered_jump_entries']) or
                    not check.get('shared_bulk_string_exercised')):
                raise ValueError('Missing Special CRT switch/copy/bulk-string regression checks')
            if stem=='runtime' and not check.get('special_final_cpu_and_storage_counters'):
                raise ValueError('Missing Special final CPU/storage diagnostics')
            binary=win/f'shared_runtime/special/wa2_special_{stem}_tests.exe'
            checks[stem]=dict(file=str(path),sha256=digest(path),binary=str(binary),binary_sha256=digest(binary))
        output=ROOT/'out/special-contents';output.mkdir(parents=True,exist_ok=True)
        strip=ndk/'toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-strip.exe'
        artifacts={}
        for name,source in {'windows':win/'WA2_Special_SDL2_x64.exe','android':android/'actual_host/libwa2_special.so'}.items():
            target=output/source.name
            run([strip,'--strip-all','-o',target,source])
            artifacts[name]=dict(file=str(target),sha256=digest(target),bytes=target.stat().st_size,
                                 unstripped_file=str(source),unstripped_sha256=digest(source))
        data=Path(artifacts['android']['file']).read_bytes()
        if data[:5]!=b'\x7fELF\x02' or struct.unpack_from('<H',data,18)[0]!=183:
            raise ValueError('Special library is not ELF64 ARM64')
        offset=struct.unpack_from('<Q',data,32)[0];size,count=struct.unpack_from('<HH',data,54)
        load_alignments=[struct.unpack_from('<Q',data,offset+n*size+48)[0] for n in range(count)
                         if struct.unpack_from('<I',data,offset+n*size)[0]==1]
        if not load_alignments or any(a<16384 for a in load_alignments):raise ValueError('Special ELF lacks 16 KiB load alignment')
        nm=ndk/'toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-nm.exe'
        symbol_audits={}
        for name,artifact in artifacts.items():
            # Zig's MinSizeRel PE has no public symbol table. Audit the actual
            # compiled archive on Windows and the final linked ELF on Android;
            # do not represent an unavailable PE symbol audit as completed.
            symbol_input=win/'shared_runtime/special/libwa2_special_aot.a' if name=='windows' else Path(artifact['unstripped_file'])
            symbols=subprocess.run([str(nm),'--defined-only','--demangle',str(symbol_input)],
                cwd=ROOT,env=env,creationflags=flags,stdout=subprocess.PIPE,stderr=subprocess.PIPE,
                check=True,timeout=60).stdout.decode('utf-8',errors='replace')
            audit=dict(method='compiled-archive-symbols' if name=='windows' else 'actual-linked-symbols',
                final_binary_symbols_available=name!='windows',special_static_functions='wa2::special::f_' in symbols,
                shared_bulk_string_kernel='wa2::aot::try_bulk_string(' in symbols,
                main_story_static_functions='wa2::aot::f_' in symbols,
                runtime_instruction_decoder='wr::Decoder::' in symbols)
            if not audit['special_static_functions'] or not audit['shared_bulk_string_kernel'] or audit['main_story_static_functions'] or audit['runtime_instruction_decoder']:
                raise ValueError('Special link contains the wrong game catalogue or an instruction decoder: '+name)
            symbol_audits[name]=audit
        proof=dict(schema='wa2.special-aot-verification.v1',input_sha256=manifest['input_sha256'],
            same_special_sources_on_all_hosts=True,compiled_shared_sources=windows_sources,
            instruction_locations=manifest['instruction_locations'],static_bodies=manifest['static_bodies'],
            recovered_callback_entries=len(callbacks['recovered_entries']),pointer_slots_audited=callbacks['pointer_slots_audited'],
            recovered_jump_entries=len(callbacks['recovered_jump_entries']),shared_bulk_string_kernel=True,
            pending_transcendental_sites=manifest['pending_transcendental_sites'],
            source_sha256=source_bindings(),checks=checks,artifacts=artifacts,elf_load_alignments=load_alignments,symbol_audits=symbol_audits,
            original_main_executable_opened=False,source_executable_executed=False,
            game_launched=False,screen_access=False,android_gameplay_validation='pending-user-controlled-run',
            android_performance_validation='pending-comparable-device-run',complete_gameplay_verified=False,passed=True)
        write_json(RECEIPT,proof)
    print(json.dumps(dict(passed=True,shared_sources=len(windows_sources),artifacts=artifacts,android_gameplay_validation='pending-user-controlled-run')),flush=True)


if __name__=='__main__':
    main()
