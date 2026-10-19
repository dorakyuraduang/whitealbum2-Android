"""Bind native-module, package and ordinary-launch evidence to this build."""
from pathlib import Path
import hashlib
import json
import re
import sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/python'))
import pefile
PREVIOUS='b9e69d3d9e9fb3916869a2c33aac074d039ce27b3f28e2048822e70701ea0a4a'
def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def read(path):return json.loads(Path(path).read_text(encoding='utf-8-sig'))
def bound(path):return dict(file=str(Path(path).resolve()),sha256=digest(path))
def verification():
    program=ROOT/'out/WA2_SDL2_x64.exe';value=digest(program)
    assert digest(ROOT/'res'/program.name)==value
    suite=read(ROOT/'logs/execution-suite.json')
    assert suite['output_sha256']==value and len(suite['fixtures'])==28 and suite['core_tests']==10 and not suite['screen_access']
    for fixture in suite['fixtures']:
        assert fixture['return_code']==0 and digest(fixture['log'])==fixture['log_sha256']
        if fixture.get('report'):assert digest(fixture['report'])==fixture['report_sha256']
    resource=read(ROOT/'logs/native-resource-tests.json');pak=read(ROOT/'logs/pak-loading-tests.json')
    assert resource['passed'] and resource['archives']==10 and resource['indexed_members']==43137
    assert resource['large_sound_bytes']==52993168 and resource['stream_cursor_above_4gib'] and resource['concurrent_reads']
    assert pak['actual_pak_loads']==200 and pak['direct_native_decoder_cases']==5 and pak['reference_hashes_match']
    assert pak['native_resources']['loads']==244 and pak['native_resources']['decodes']==251 and pak['native_image_decoder_cases']==5
    assert pak['native_full_image_load_cases']==40 and pak['image_loader_edge_cases']==6 and pak['original_image_loader_matches']
    assert pak['transactional_decode_failure_cases']==6
    assert pak['native_resources']['single_pass_image_decodes']==41
    from compare_resource_loading import verification as check_resource_comparison
    resource_comparison=check_resource_comparison()
    assert resource['dictionary_reference_cases']==2000 and resource['archive_open_payload_reads']==0
    assert resource['fixed_copy_boundary_cases']==5200
    assert pak['native_resources']['errors']==pak['native_resources']['declined']==0 and pak['regions_restored_after_every_load']
    vorbis=read(ROOT/'logs/native-vorbis-tests.json');assert vorbis['passed'] and len(vorbis['tracks'])==3 and vorbis['loop_restart_matches']
    assert vorbis['fragmentation_matches'] and vorbis['original_format_query_preserved'] and vorbis['output_canary_unchanged']
    assert vorbis['non_looping_eof_matches']
    assert vorbis['native_lifecycle'] and vorbis['metadata_matches_original'] and vorbis['constructor_zeroes_object']
    assert vorbis['repeated_cleanup'] and vorbis['cached_format_matches'] and vorbis['paged_memory_matches']
    assert all(t['maximum_sample_error']<=16 and t['rms_sample_error']<=1.5 for t in vorbis['tracks'])
    from verify_voice_codec import check as check_voice
    assert check_voice()==read(ROOT/'logs/native-voice-codec-verification.json')
    portable=read(ROOT/'logs/portable-engine-verification.json');assert portable['passed']
    for name,expected in portable['source_sha256'].items():assert digest(ROOT/name)==expected
    for item in portable['objects']:assert digest(item['object'])==item['object_sha256']
    assert digest(portable['native_tests']['file'])==portable['native_tests']['sha256']
    for item in [portable['standalone_vorbis']['report'],portable['standalone_vorbis']['program']]:assert digest(item['file'])==item['sha256']
    assert portable['standalone_vorbis']['identification_headers']>=38844 and portable['standalone_vorbis']['audio_archives']==3
    from verify_native_generation import check
    check()
    manifest=read(ROOT/'port/native_linear_manifest.json');functions=read(ROOT/'port/native_functions.json')
    assert manifest['source_replaced_instructions']>1361 and len(functions['functions'])==10 and manifest['checked_cpp_fallback_retained']
    from verify_retired_modules import verify as check_retired
    retired=check_retired();assert retired==read(ROOT/'logs/retired-modules-verification.json')
    registered={int(x,16) for source in ['port/native_resources.cpp','port/native_vorbis.cpp'] for x in re.findall(r'register_native_function\(0x([0-9a-f]+),',(ROOT/source).read_text())}
    assert registered=={f['start'] for f in functions['functions']}
    graph=read(ROOT/'logs/cfg.json')
    for function in functions['functions']:
        start,end=function['start'],function['end']
        assert not any(isinstance(e.get('to'),int) and start<e['to']<end and not start<=e['from']<end for e in graph['edges'])
    from verify_fault_table import verify
    faults=verify();assert faults==read(ROOT/'logs/compact-fault-table-tests.json')
    startup=read(ROOT/'logs/fixed-startup-verification.json')
    assert startup['output_sha256']==value and startup['alive_at_test_stop'] and not startup['error'] and startup['elapsed_seconds']>=75
    assert not startup['screen_access'] and not startup['automated_input'] and digest(startup['report']['file'])==startup['report']['sha256']
    runtime=read(startup['report']['file']);runtime=runtime.get('process',runtime)
    actual=runtime['native_resources'];assert actual['loads'] and actual['image_decodes'] and actual['errors']==0
    assert actual['image_loads'] and actual['image_index_cache_hits'] and actual['image_index_opens']<actual['image_loads']
    assert actual['declined']==0 and actual['image_missing_archives']>0
    assert actual['single_pass_image_decodes']==actual['image_decodes']
    assert runtime['resource_graphics']['surface_load_calls'] and not runtime['resource_graphics']['screen_capture']
    assert runtime['directsound']['backend']=='sdl2-native-pcm' and runtime['directsound']['callback_errors']==0
    codec=runtime['native_vorbis'];assert codec['calls'] and codec['output_bytes'] and codec['errors']==codec['declined']==0
    assert codec['constructors'] and codec['format_queries']
    assert codec['requests']<=codec['input_bytes']//4096+1000 and codec['exhausted_calls']<1000
    assert not runtime['frame_timing']['enabled'] and not runtime['resource_load_trace']['enabled']
    assert 'api_latency' not in runtime and 'runtime_report_timing' not in runtime and runtime['direct3d9']['frames_captured']==0
    audio=read(ROOT/'logs/native-audio-launch-verification.json')
    assert audio['passed'] and audio['output_sha256']==value and audio['stream']['refill_deadlines_met']
    logo=read(ROOT/'logs/logo-fade-verification.json');assert logo['passed'] and logo['output_sha256']==value and not logo['screen_access']
    for observation in [logo['before'],*logo['after']]:assert digest(observation['evidence']['file'])==observation['evidence']['sha256']
    previous=ROOT/'build/runtime-backups'/PREVIOUS/program.name;assert digest(previous)==PREVIOUS
    from verify_package_size import verification as check_package
    assert check_package()==read(ROOT/'logs/package-size-verification.json')
    image=pefile.PE(str(program));imports=sorted(i.dll.decode().lower() for i in image.DIRECTORY_ENTRY_IMPORT)
    assert 'dsound.dll' not in imports and 'd3d9.dll' not in imports
    original=ROOT/'WA2_chs.exe';assert digest(original)==functions['input_sha256']
    package=dict(previous=bound(previous),previous_bytes=previous.stat().st_size,bytes=program.stat().st_size,
        bytes_saved=previous.stat().st_size-program.stat().st_size,original_bytes=original.stat().st_size,
        size_delta_bytes=program.stat().st_size-previous.stat().st_size,
        original_size_target_met=program.stat().st_size<=original.stat().st_size)
    sources=['engine/resource_archive.hpp','engine/resource_archive.cpp','engine/vorbis_stream.hpp','engine/vorbis_stream.cpp','third_party/stb/stb_vorbis.c','third_party/stb/README.md','engine/CMakeLists.txt','platform/file_io.hpp','platform/file_io.cpp',
        'platform/clock.hpp','platform/clock.cpp','platform/timer_resolution.cpp','platform/portable.cmake',
        'port/native_resources.hpp','port/native_resources.cpp','port/native_vorbis.hpp','port/native_vorbis.cpp','port/native_functions.json','port/CMakeLists.txt','port/wa2_win32.cpp',
        'port/specialized_entries.inc','port/linear_faults.cpp','scripts/generate_native_backend_v2.py','scripts/verify_native_generation.py',
        'scripts/verify_fault_table.py','scripts/stage_runtime.py','scripts/verify_execution.py','scripts/verify_logo_fade.py','scripts/trace_logo_fade.py','scripts/compare_resource_loading.py',
        'scripts/inspect_binary_size.py','scripts/link_release_pe.py','scripts/verify_package_size.py','tools/WindowsRecompTool-main/tools/generate_fp_asm.py',
        'scripts/retired_modules.py','scripts/verify_retired_modules.py','port/retired_modules.json','scripts/compact_fallback.py','port/compact_fallback.cmake',
        'tests/vorbis_stream.cpp','tests/vorbis_portable.cpp','scripts/make_voice_codec_samples.py','scripts/verify_voice_codec.py','tests/resources.cpp','tests/pak_loading.cpp','tests/port.cpp',
        'tools/WindowsRecompTool-main/include/winrecomp/process.hpp','tools/WindowsRecompTool-main/src/runtime/process.cpp']
    return dict(schema='wa2.native-engine.v1',output_sha256=value,package=package,source_functions=functions['functions'],
        source_replaced_native_instruction_locations=manifest['source_replaced_instructions'],source_replaced_functions=10,checked_cpp_fallback_retained=True,
        retired_modules=bound(ROOT/'logs/retired-modules-verification.json'),retired_cpp_instructions=retired['removed_cpp_instructions'],
        full_old_codec_in_release=False,full_old_codec_in_test_oracle=True,
        package_size_audit=bound(ROOT/'logs/package-size-verification.json'),
        native_resource_regression=bound(ROOT/'logs/native-resource-tests.json'),native_pak_regression=bound(ROOT/'logs/pak-loading-tests.json'),
        resource_loading_comparison=bound(ROOT/'logs/resource-loading-comparison.json'),resource_loading_summary=resource_comparison['summary'],
        native_portable_modules=bound(ROOT/'logs/portable-engine-verification.json'),execution_suite=bound(ROOT/'logs/execution-suite.json'),
        fault_table=bound(ROOT/'logs/compact-fault-table-tests.json'),ordinary_launch=bound(ROOT/'logs/fixed-startup-verification.json'),
        actual_native_resource_counters=actual,native_audio=bound(ROOT/'logs/native-audio-launch-verification.json'),
        native_vorbis=bound(ROOT/'logs/native-vorbis-tests.json'),actual_native_vorbis_counters=codec,
        native_voice_codec=bound(ROOT/'logs/native-voice-codec-verification.json'),logo_fade=bound(ROOT/'logs/logo-fade-verification.json'),
        source_files=[bound(ROOT/name) for name in sources],native_import_dlls=imports,
        entire_engine_reconstructed=False,all_platform_apis_portable=False,original_performance_proven=False,
        android_device_verified=False,screen_access=False,actual_gameplay_fast_forward_verified=False,passed=True)
if __name__=='__main__':
    result=verification();(ROOT/'logs/native-engine-verification.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(dict(package=result['package'],actual_native_resources=result['actual_native_resource_counters'],passed=True),indent=2))
