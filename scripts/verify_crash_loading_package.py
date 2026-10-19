"""Bind the observed crash, real PAK regressions and package size to one build."""
from pathlib import Path
import hashlib
import json
import sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/python'))
import pefile
BEFORE='6550d8b778a45dd89daf2bf0c1a82970a915777239ef4ab2dc1fba9da01d7061'

def read(path):return json.loads(Path(path).read_text(encoding='utf-8-sig'))
def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def bound(path):return dict(file=str(Path(path).resolve()),sha256=digest(path))

def verification():
    program=ROOT/'out/WA2_SDL2_x64.exe';value=digest(program);assert digest(ROOT/'res/WA2_SDL2_x64.exe')==value
    suite_path=ROOT/'logs/execution-suite.json';suite=read(suite_path);assert suite['output_sha256']==value
    assert suite['compact_library_sha256']==digest(ROOT/'build/wa2-win64/wa2_port/libwa2_compact_ops.a')
    assert len(suite['fixtures'])==25 and suite['core_tests']==10 and not suite['screen_access']
    for fixture in suite['fixtures']:
        assert fixture['return_code']==0 and digest(fixture['log'])==fixture['log_sha256']
        if fixture.get('report'):assert digest(fixture['report'])==fixture['report_sha256']
    crash_path=ROOT/'logs/user-crash-20261016-142650/runtime.json';crash=read(crash_path)
    assert crash['error']=='untranslated instruction target at guest address 4408656'
    graph=read(ROOT/'logs/cfg.json');assert any(i['address']==0x434550 and i['bytes']=='c3' for i in graph['instructions'])
    sites={0x4589da,0x45e3d6,0x45ff7f,0x461454,0x46147c}
    assert {e['from'] for e in graph['edges'] if e['kind']=='data_callback_initial' and e['to']==0x434550}==sites
    callbacks=read(ROOT/'logs/script-handler-tests.json');assert callbacks['passed'] and callbacks['data_callback_cases']==12 and callbacks['real_data_callback_call_sites']==5
    sound_crash_path=ROOT/'logs/user-crash-20261016-155129/runtime.json';sound_crash=read(sound_crash_path)
    assert sound_crash['error']=='DirectSound: sound lock size limit at guest address 4026541152'
    assert sound_crash['process']['recent_transfers'][-1]['arguments'][2]==52992000
    audio=read(ROOT/'logs/audio-buffers-tests.json');assert audio['passed'] and audio['pcm_bytes']==52992000 and audio['large_load_release_cycles']==8
    assert audio['regions_restored_after_every_cycle'] and audio['full_pcm_round_trip'] and audio['audio']['backend']=='sdl2-native-pcm'
    assert audio['audio']['largest_lock_bytes']==52992000 and audio['audio']['staging_bytes']==0 and not audio['audio']['callback_errors']
    pcm=read(ROOT/'logs/native-pcm-tests.json');assert pcm['passed'] and pcm['checked_stream_frames']==8640000 and pcm['stream_refills']==180
    assert not pcm['guest_runtime_dependency'] and not pcm['windows_dependency']
    memory_before=read(ROOT/'logs/resource-memory-before.json');memory=read(ROOT/'logs/resource-memory-tests.json')
    assert memory['passed'] and memory['cycles']==memory_before['cycles']==32 and memory['resource_bytes']==52992000
    assert memory['large_commit_calls']==memory['large_decommit_calls']==32
    assert memory_before['large_commit_calls']==memory_before['large_decommit_calls']==414016
    assert memory['all_pages_released'] and memory['zero_fill_and_partial_commit_preserved']
    from verify_fault_table import verify as verify_fault_table
    faults=verify_fault_table();assert faults==read(ROOT/'logs/compact-fault-table-tests.json')
    arm64=ROOT/'build/audio-before/pcm_audio-arm64.o';arm64_data=arm64.read_bytes()
    assert arm64_data[:5]==b'\x7fELF\x02' and int.from_bytes(arm64_data[18:20],'little')==183
    flow=read(ROOT/'build/data-callback-controlflow/report.json');assert flow['total_vectors']==1280 and flow['negative_table_tests']==11
    pak=read(ROOT/'logs/pak-loading-tests.json');assert pak['passed'] and pak['exact_loop_cases']==1000 and pak['actual_pak_loads']==200
    assert pak['reference_hashes_match'] and pak['regions_restored_after_every_load'] and not pak['screen_access']
    assert pak['native_cpu_kernels']['pak_linear_bytes']>pak['decoded_bytes']*.9
    samples=read(ROOT/'logs/resource-samples.json');assert len(samples['samples'])==5 and samples['initial_ring_fill']==0
    assert digest(samples['fixture'])==samples['fixture_sha256']
    compact_path=ROOT/'build/wa2-win64/compact-fallback/manifest.json';compact=read(compact_path)
    assert compact['instructions']==len(graph['instructions'])==139987 and compact['every_body_expanded_and_matched']
    assert compact['original_byte_guards_preserved'] and compact['shared_embedded_image']
    assert not compact['runtime_instruction_decoder'] and not compact['runtime_bytecode']
    for name,expected in compact['source_sha256'].items():assert digest(ROOT/'generated/wa2-win64'/name)==expected
    before=ROOT/'build/runtime-backups'/BEFORE/program.name;assert digest(before)==BEFORE
    image=pefile.PE(str(program));assert image.FILE_HEADER.Machine==0x8664
    assert 'dsound.dll' not in [i.dll.decode().lower() for i in image.DIRECTORY_ENTRY_IMPORT]
    config=(ROOT/'build/wa2-win64/CMakeCache.txt').read_text()
    for line in ('WA2_COMPACT_FALLBACK:BOOL=ON','WA2_NATIVE_PROFILE_POINTS:BOOL=OFF','WA2_PAGED_NATIVE_BACKEND:BOOL=OFF','WA2_CPP_BLOCK_BACKEND:BOOL=OFF'):assert line in config
    original=ROOT/'WA2_chs.exe';assert digest(original)==compact['input_sha256']
    assert program.read_bytes().count(original.read_bytes())==1
    package=dict(before=bound(before),before_bytes=before.stat().st_size,after_bytes=program.stat().st_size,
        original_bytes=original.stat().st_size,reduction_percent=100*(1-program.stat().st_size/before.stat().st_size),
        shared_fallback_functions=compact['shared_functions'],instruction_coverage_removed=False,
        runtime_decoder=False,runtime_bytecode=False,original_size_target_met=program.stat().st_size<=original.stat().st_size,
        sections=[dict(name=s.Name.decode().rstrip('\0'),bytes=s.SizeOfRawData) for s in image.sections])
    assert package['after_bytes']<package['before_bytes']*.8
    previous=ROOT/'build/runtime-backups/6817246451ec86560b53b558529ff88f5db2814137e21c53b141d7db06635dee'/program.name
    package['previous_user_build']=bound(previous);package['previous_user_build_bytes']=previous.stat().st_size
    package['further_bytes_saved']=previous.stat().st_size-program.stat().st_size
    assert package['further_bytes_saved']>0
    startup_path=ROOT/'logs/fixed-startup-verification.json';startup=read(startup_path)
    assert startup['output_sha256']==value and not startup['error'] and startup['elapsed_seconds']>=15
    assert startup['alive_at_test_stop'] and not startup['screen_access'] and not startup['fps_analysis']
    assert digest(startup['report']['file'])==startup['report']['sha256']
    launch_path=ROOT/'logs/native-audio-launch-verification.json';launch=read(launch_path)
    assert launch['passed'] and launch['output_sha256']==value and launch['audio_backend']=='sdl2-native-pcm'
    assert launch['callback_errors']==0 and launch['stream']['refill_deadlines_met']
    assert launch['stream']['observed_stream_seconds']>=30 and digest(launch['runtime']['file'])==launch['runtime']['sha256']
    large_startup_path=ROOT/'logs/fixed-startup-75-verification.json';large_startup=read(large_startup_path)
    assert large_startup['output_sha256']==value and large_startup['alive_at_test_stop'] and not large_startup['error']
    assert large_startup['elapsed_seconds']>=75 and not large_startup['screen_access']
    assert digest(large_startup['report']['file'])==large_startup['report']['sha256']
    large_state=read(large_startup['report']['file']);large_state=large_state.get('process',large_state)
    assert large_state['directsound']['largest_lock_bytes']==52992000 and large_state['directsound']['callback_errors']==0
    sources=['port/fast_paths.cpp','port/specialized_entries.inc','port/native_backend.cpp','port/native_backend_metadata.cpp',
        'port/CMakeLists.txt','port/compact_fallback.cmake','scripts/compact_fallback.py','scripts/generate_native_backend_v2.py',
        'scripts/make_resource_samples.py','tests/pak_loading.cpp','tests/script_handlers.cpp',
        'tools/WindowsRecompTool-main/src/cfg.cpp','tools/WindowsRecompTool-main/tests/controlflow.py',
        'engine/pcm_audio.cpp','engine/pcm_audio.hpp','engine/CMakeLists.txt','platform/sdl_audio.cpp','platform/sdl_audio.hpp',
        'port/audio.cpp','port/audio_abi.hpp','port/guest_api.hpp','port/sdl_backend.cmake','port/linear_faults.cpp',
        'tests/audio_buffers.cpp','tests/pcm_audio.cpp','tests/resource_memory.cpp',
        'tools/WindowsRecompTool-main/include/winrecomp/memory.hpp','tools/WindowsRecompTool-main/include/winrecomp/native_memory.hpp',
        'tools/WindowsRecompTool-main/src/runtime/native_memory.cpp','scripts/verify_fault_table.py','scripts/verify_native_generation.py',
        'scripts/stage_runtime.py','scripts/verify_execution.py','scripts/verify_audio_launch.py','scripts/generate_project.py','build.ps1']
    return dict(schema='wa2.crash-loading-package.v1',output_sha256=value,original_crash=bound(crash_path),
        repaired_target=0x434550,real_callback_call_sites=sorted(sites),callback_regression=bound(ROOT/'logs/script-handler-tests.json'),
        controlflow_regression=bound(ROOT/'build/data-callback-controlflow/report.json'),pak_loading=pak,
        pak_loading_evidence=bound(ROOT/'logs/pak-loading-tests.json'),resource_samples=bound(ROOT/'logs/resource-samples.json'),
        compact_fallback=bound(compact_path),execution_suite=bound(suite_path),package=package,
        standalone_startup=bound(startup_path),sound_crash=bound(sound_crash_path),large_sound_regression=bound(ROOT/'logs/audio-buffers-tests.json'),
        actual_large_sound_startup=bound(large_startup_path),
        actual_game_audio=bound(launch_path),
        native_pcm=bound(ROOT/'logs/native-pcm-tests.json'),native_pcm_arm64_object=bound(arm64),
        resource_memory_before=bound(ROOT/'logs/resource-memory-before.json'),resource_memory=bound(ROOT/'logs/resource-memory-tests.json'),
        compact_fault_table=bound(ROOT/'logs/compact-fault-table-tests.json'),
        entire_engine_reconstructed=False,all_platform_apis_portable=False,
        source_files=[bound(ROOT/x) for x in sources],screen_access=False,automated_input=False,
        actual_gameplay_fast_forward_verified=False,all_possible_crashes_fixed=False)

if __name__=='__main__':
    result=verification();(ROOT/'logs/crash-loading-package-verification.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(dict(repaired_target=hex(result['repaired_target']),pak_loading=result['pak_loading'],package=result['package']),indent=2))
