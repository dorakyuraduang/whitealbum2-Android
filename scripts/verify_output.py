"""Verify the native output architecture and bind evidence to file hashes."""
from pathlib import Path
import hashlib
import json
import sys
import zipfile

ROOT=Path(__file__).resolve().parent.parent
sys.path.insert(0,str(ROOT/'tools'/'python'))
import pefile

def main():
    original=ROOT/'WA2_chs.exe';output=ROOT/'out'/'WA2_SDL2_x64.exe'
    profile=json.loads((ROOT/'config'/'wa2.json').read_text())
    manifest=json.loads((ROOT/'generated'/'wa2-win64'/'manifest.json').read_text())
    source_hash=hashlib.sha256(original.read_bytes()).hexdigest()
    if source_hash!=profile['sha256'] or source_hash!=manifest['sha256']:raise ValueError('Original input/profile/translation mismatch')
    if manifest['partial']:raise ValueError('Refusing to call a trap-based partial translation a native build')
    native=pefile.PE(str(output))
    if native.FILE_HEADER.Machine!=0x8664 or native.OPTIONAL_HEADER.Magic!=0x20b:raise ValueError('Output is not an AMD64 PE32+ executable')
    imports=sorted(item.dll.decode('ascii').lower() for item in native.DIRECTORY_ENTRY_IMPORT)
    if any(name=='d3d9.dll' or name.startswith('d3dx9_') for name in imports):raise ValueError('Native DX9 graphics dependencies remain')
    if 'dsound.dll' in imports:raise ValueError('Native DirectSound dependency remains')
    dependencies=json.loads((ROOT/'tools'/'sdl-graphics-dependencies.json').read_text())
    embedded=(ROOT/'WA2_chs.exe').read_bytes() in output.read_bytes()
    if not embedded:raise ValueError('Standalone output is missing its immutable PE data')
    for item in dependencies:
        archive=ROOT/'tools'/item['archive']
        if hashlib.sha256(archive.read_bytes()).hexdigest()!=item['sha256']:raise ValueError(f'Graphics dependency archive changed: {archive}')
    evidence={
        'schema':'wa2.win64-build.v1',
        'input_sha256':source_hash,
        'output_sha256':hashlib.sha256(output.read_bytes()).hexdigest(),
        'output_file':str(output),
        'output_bytes':output.stat().st_size,
        'machine':'AMD64 (0x8664)',
        'format':'PE32+ (0x20b)',
        'original_generated_instruction_locations':manifest['instructions'],
        'translated_instruction_locations':json.loads((ROOT/'build/wa2-win64/compact-fallback/manifest.json').read_text())['release_instructions'],
        'retired_codec_test_oracle_only':True,
        'partial_instruction_translation':False,
        'whole_program_coverage_proven':False,
        'original_required_for_image_data':False,
        'immutable_original_image_embedded':embedded,
        'native_pointer_bits':64,
        'guest_serialized_pointer_bits':32,
        'graphics_backend':'SDL2 2.32.10 / OpenGL / MojoShader',
        'audio_backend':'Native C++ PCM mixer / SDL2 output',
        'resource_backend':'Native C++ KCAP/LAC reader and LZSS decoder',
        'clock_backend':'C++ monotonic and system clocks / platform timer policy',
        'entire_engine_reconstructed':False,
        'all_platform_apis_portable':False,
        'graphics_libraries_statically_linked':True,
        'native_import_dlls':imports,
        'graphics_dependencies':dependencies,
        'upstream':{'url':'https://github.com/Zippingrocks/WindowsRecompTool','commit':zipfile.ZipFile(ROOT/'tools'/'winrecomp.zip').comment.decode()},
        'runtime_status':'See logs/runtime.json; a successful link does not establish game startup/playability.',
    }
    for name,path in [('integer_conformance',ROOT/'build'/'wa2-integer-tests'/'report.json'),('flags_conformance',ROOT/'build'/'flags-tests'/'report.json'),('sse_conformance',ROOT/'build'/'sse-tests'/'report.json'),('memory_conformance',ROOT/'build'/'memory-tests'/'report.json')]:
        if path.exists():
            test=json.loads(path.read_text())
            evidence[name]={'total_vectors':test['total_vectors'],'report':str(path),'bound_to_current_output':False}
    graphics_report=ROOT/'build'/'graphics-tests'/'report.json'
    if graphics_report.exists():
        result=json.loads(graphics_report.read_text())
        evidence['graphics_test']={'report':str(graphics_report),'backend':result['direct3d9']['backend'],'draw_calls':result['direct3d9']['draw_calls'],'bound_to_current_output':False}
    thread_report=ROOT/'build'/'thread-tests'/'report.json'
    if thread_report.exists():
        result=json.loads(thread_report.read_text())
        evidence['thread_test']={'report':str(thread_report),'backend':result['threads']['backend'],'threads_completed':result['threads']['threads_completed'],'bound_to_current_output':False}
    port_library=ROOT/'build'/'wa2-win64'/'wa2_port'/'libwa2_port.a'
    evidence['port_library_sha256']=hashlib.sha256(port_library.read_bytes()).hexdigest()
    native_manifest=ROOT/'port/native_backend_manifest.json'
    if native_manifest.exists():
        backend=json.loads(native_manifest.read_text())
        if backend['input_sha256']!=source_hash:raise ValueError('Native backend targets another input image')
        evidence['native_execution_backend']={'manifest':str(native_manifest),
            'manifest_sha256':hashlib.sha256(native_manifest.read_bytes()).hexdigest(),
            'instruction_locations':backend['instructions'],'entry_blocks':backend['entries'],
            'pages':len(backend['pages']),'registers_resident':backend['registers_resident'],
            'cross_page_native_control_flow':backend['cross_page_native_control_flow'],
            'all_instructions_direct_native':False,'gameplay_verified':False}
    runtime_path=ROOT/'logs'/'runtime-evidence.json'
    if runtime_path.exists():
        runtime=json.loads(runtime_path.read_text())
        if runtime['output_sha256']==evidence['output_sha256']:
            report=runtime['report'];state=report.get('process',report)
            evidence['runtime']={'host_return_code':runtime['host_return_code'],'stop':report.get('error'),'native_instructions':state.get('native_instructions'),'api_calls':state.get('api_calls'),'report':str(runtime_path),'diagnostic_stop':state.get('diagnostic_stop',False),'presented_frames':state.get('direct3d9',{}).get('presents'),'gameplay_verified':False}
    startup_path=ROOT/'logs'/'sdl-startup-verification.json'
    if startup_path.exists():
        startup=json.loads(startup_path.read_text())
        if startup['output_sha256']==evidence['output_sha256']:
            evidence['startup_verification']=startup
            evidence['runtime_status']='Bounded offscreen rendering diagnostic passed; actual window visibility and full gameplay require separate verification.'
    performance_path=ROOT/'logs'/'performance-comparison.json'
    if performance_path.exists():
        performance=json.loads(performance_path.read_text())
        if performance['after']['output_sha256']==evidence['output_sha256']:evidence['performance_verification']=performance
    native_messages_path=ROOT/'logs'/'native-message-fix-verification.json'
    if native_messages_path.exists():
        native_messages=json.loads(native_messages_path.read_text())
        if native_messages['output_sha256']==evidence['output_sha256']:
            evidence['native_message_verification']=native_messages
    mouse_activate_path=ROOT/'logs'/'mouseactivate-fix-verification.json'
    if mouse_activate_path.exists():
        mouse_activate=json.loads(mouse_activate_path.read_text())
        if mouse_activate['output_sha256']==evidence['output_sha256']:
            evidence['mouse_activate_verification']=mouse_activate
    normal_path=ROOT/'logs'/'normal-launch-verification.json'
    if normal_path.exists():
        normal=json.loads(normal_path.read_text())
        if normal['output_sha256']==evidence['output_sha256']:
            for item in [normal['runtime_report'],normal['menu_screenshot'],normal['launch_report']]:
                if hashlib.sha256(Path(item['file']).read_bytes()).hexdigest()!=item['sha256']:
                    raise ValueError('Normal-launch evidence file changed: '+item['file'])
            if normal['host_return_code']!=0 or not normal['menu_visually_inspected'] or not normal['normal_exit_verified']:
                raise ValueError('Normal-launch evidence does not establish menu and normal exit')
            evidence['normal_launch_verification']=normal
            evidence['runtime_status']='Ordinary run.cmd with res: visible native title menu and normal confirmed exit verified without --frames, --snapshot or --profile. Full gameplay remains unverified.'
    for label,name in [('bridge_suite','bridge-suite.json'),('embedded_launch_tests','embedded-launch-tests.json'),('startup_optimization','startup-optimization.json'),('standalone_launch','standalone-launch-verification.json')]:
        path=ROOT/'logs'/name
        if path.exists():
            result=json.loads(path.read_text())
            if result.get('output_sha256')==evidence['output_sha256']:
                if label=='standalone_launch':
                    for item in [result['runtime_report'],result['menu_screenshot'],result['launch_report']]:
                        if hashlib.sha256(Path(item['file']).read_bytes()).hexdigest()!=item['sha256']:raise ValueError('Standalone evidence changed: '+item['file'])
                    if not result['menu_visually_inspected'] or not result['normal_exit_verified']:raise ValueError('Standalone launch was not verified')
                evidence[label]={'report':str(path),'sha256':hashlib.sha256(path.read_bytes()).hexdigest()}
    script_path=ROOT/'logs/script-fix-verification.json'
    if script_path.exists():
        script=json.loads(script_path.read_text())
        if script.get('output_sha256')==evidence['output_sha256']:
            if not script['all_non_null_handlers_translated'] or not script['code_regressions_passed']:
                raise ValueError('Script callback recovery was not verified')
            for item in script['evidence_files']:
                if hashlib.sha256(Path(item['file']).read_bytes()).hexdigest()!=item['sha256']:
                    raise ValueError('Script callback evidence changed: '+item['file'])
            evidence['script_handler_verification']={'report':str(script_path),'sha256':hashlib.sha256(script_path.read_bytes()).hexdigest(),
                                                      'translated_handlers':script['translated_handlers'],
                                                      'gameplay_verified':False,'screen_access_used':False}
            evidence['runtime_status']='Script callback code regressions passed. Actual beginning-gameplay flow has not been verified for this build.'
    bgm_path=ROOT/'logs/bgm-stream-verification.json'
    if bgm_path.exists():
        bgm=json.loads(bgm_path.read_text())
        if bgm.get('output_sha256')==evidence['output_sha256']:
            bound_files=[bgm['baseline']['program'],*bgm['baseline']['runtime_sources']]
            for run in bgm['fixed']:
                if not run['refill_deadlines_met'] or not run['baseline_pcm_sample_prefix_matches']:
                    raise ValueError('BGM buffer continuity verification failed')
                bound_files.extend([run['launch_report'],*run['runtime_sources']])
            for item in bound_files:
                if hashlib.sha256(Path(item['file']).read_bytes()).hexdigest()!=item['sha256']:
                    raise ValueError('BGM evidence changed: '+item['file'])
            evidence['bgm_stream_verification']={'report':str(bgm_path),'sha256':hashlib.sha256(bgm_path.read_bytes()).hexdigest(),
                                                  'refill_deadlines_met':True,'pcm_sample_prefix_matches':True}
    execution_path=ROOT/'logs/execution-suite.json'
    if execution_path.exists():
        execution=json.loads(execution_path.read_text())
        if execution.get('output_sha256')==evidence['output_sha256']:
            runtime_library=ROOT/'build/wa2-win64/winrecomp/libwinrecomp_guest.a'
            if execution['port_library_sha256']!=evidence['port_library_sha256'] or execution['runtime_library_sha256']!=hashlib.sha256(runtime_library.read_bytes()).hexdigest():
                raise ValueError('Execution tests used different native libraries')
            if execution['native_helpers_library_sha256']!=hashlib.sha256((ROOT/'build/wa2-win64/winrecomp/libwinrecomp_native.a').read_bytes()).hexdigest():
                raise ValueError('Execution tests used different floating-point helpers')
            if execution.get('compact_library_sha256')!=hashlib.sha256((ROOT/'build/wa2-win64/wa2_port/libwa2_compact_ops.a').read_bytes()).hexdigest():
                raise ValueError('Execution tests used a different compact fallback library')
            for field,name in [('native_engine_library_sha256','engine/libwa2_engine.a'),('platform_library_sha256','libwa2_platform.a'),('sdl_audio_library_sha256','libwa2_sdl_audio.a'),('sdl_library_sha256','sdl2/libSDL2.a'),('codec_oracle_library_sha256','libwa2_codec_oracle.a')]:
                if execution.get(field)!=hashlib.sha256((ROOT/'build/wa2-win64/wa2_port'/name).read_bytes()).hexdigest():
                    raise ValueError('Execution tests used a different platform/engine library: '+name)
            if execution['retired_module_proof_sha256']!=hashlib.sha256((ROOT/'logs/retired-modules-verification.json').read_bytes()).hexdigest():
                raise ValueError('Execution tests used a different retired-module proof')
            if len(execution['fixtures'])!=28 or execution['core_tests']!=10 or execution['screen_access']:
                raise ValueError('Incomplete non-window execution regressions')
            files=[(execution['core_log'],execution['core_log_sha256'])]
            for fixture in execution['fixtures']:
                if fixture['return_code']!=0:raise ValueError('Execution fixture failed')
                files.append((fixture['log'],fixture['log_sha256']))
                if 'report' in fixture:files.append((fixture['report'],fixture['report_sha256']))
            for file,expected in files:
                if hashlib.sha256(Path(file).read_bytes()).hexdigest()!=expected:
                    raise ValueError('Execution evidence changed: '+file)
            evidence['execution_regressions']={'report':str(execution_path),'sha256':hashlib.sha256(execution_path.read_bytes()).hexdigest(),
                                                'fixtures':28,'core_tests':10,'screen_access':False,'gameplay_verified':False}
    execution_performance_path=ROOT/'logs/execution-performance.json'
    if execution_performance_path.exists():
        performance=json.loads(execution_performance_path.read_text())
        if performance.get('output_sha256')==evidence['output_sha256']:
            from compare_execution_performance import summarize
            if performance['sampling_enabled'] or performance['screen_access'] or performance['screenshots']:
                raise ValueError('Execution performance requires sampling/capture disabled')
            for run in [performance['before'],performance['after'],*performance['after_repeats']]:
                if summarize(Path(run['evidence']['file']))!=run:
                    raise ValueError('Execution performance evidence no longer matches Present logs')
            evidence['execution_performance']={'report':str(execution_performance_path),
                'sha256':hashlib.sha256(execution_performance_path.read_bytes()).hexdigest(),
                'comparisons':performance['comparisons'],'sustained_30fps':performance['sustained_30fps'],
                'screen_access':False,'gameplay_verified':False}
            evidence['runtime_status']='Execution regressions passed and unprofiled Present timing logged. See sustained_30fps; full gameplay and normal-launch visibility remain unverified for this build.'
    native_performance_path=ROOT/'logs/native-performance.json'
    if native_performance_path.exists():
        performance=json.loads(native_performance_path.read_text())
        if performance.get('output_sha256')==evidence['output_sha256']:
            from compare_native_performance import summarize,comparisons
            if performance['screen_access'] or performance['profile_enabled']:
                raise ValueError('Native performance evidence enabled screen access or sampling')
            for run in [performance['before'],*performance['after']]:
                if summarize(Path(run['evidence']['file']),performance.get('program_archives',{}).get(run['output_sha256']))!=run:
                    raise ValueError('Native performance evidence no longer matches Present logs')
            for excluded in performance.get('excluded_diagnostics',[]):
                run=excluded['run']
                if summarize(Path(run['evidence']['file']),performance.get('program_archives',{}).get(run['output_sha256']))!=run:
                    raise ValueError('Excluded native diagnostic changed')
            if comparisons(performance['before'],performance['after'])!=performance['comparisons']:
                raise ValueError('Native performance comparison changed')
            evidence['native_performance']={'report':str(native_performance_path),
                'sha256':hashlib.sha256(native_performance_path.read_bytes()).hexdigest(),
                'comparisons':performance['comparisons'],'sustained_30fps':performance['sustained_30fps'],
                'screen_access':False,'normal_exit_verified':False,'gameplay_verified':False}
            evidence['runtime_status']='Native-register backend passed code regressions and fixed-duration Present diagnostics. See sustained_30fps; normal-launch visibility and full gameplay remain unverified.'
    patch_path=ROOT/'logs/runtime-patch.json'
    if patch_path.exists():
        patch=json.loads(patch_path.read_text())
        if not patch.get('git_apply_check') or not patch.get('normalized_sources_match'):
            raise ValueError('Runtime patch was not replayed against its upstream archive')
        for item,key in [('archive','archive_sha256'),('patch','patch_sha256')]:
            if hashlib.sha256(Path(patch[item]).read_bytes()).hexdigest()!=patch[key]:raise ValueError('Runtime patch input changed')
        checkout=ROOT/'tools/WindowsRecompTool-main'
        for name,value in patch['source_sha256'].items():
            path=checkout/name
            if not path.resolve().is_relative_to(checkout.resolve()):raise ValueError('Invalid runtime source path')
            actual=hashlib.sha256(path.read_bytes()).hexdigest() if path.exists() else None
            if actual!=value:raise ValueError('Runtime patch sources changed: '+name)
        evidence['runtime_patch']={'report':str(patch_path),'sha256':hashlib.sha256(patch_path.read_bytes()).hexdigest(),
            'patch_sha256':patch['patch_sha256'],'git_apply_check':True,'files':len(patch['files'])}
    matched_path=ROOT/'logs/original-native-performance.json'
    if matched_path.exists():
        matched=json.loads(matched_path.read_text())
        if matched.get('output_sha256')==evidence['output_sha256']:
            from compare_original_native import comparison
            original=Path(matched['original']['evidence']['file'])
            native=[Path(item['evidence']['file']) for item in matched['native']]
            ordinary=Path(matched['ordinary']['evidence']['file']) if matched.get('ordinary') else None
            if comparison(original,native,ordinary)!=matched:
                raise ValueError('Matched original/native performance evidence changed')
            evidence['original_native_performance']={'report':str(matched_path),
                'sha256':hashlib.sha256(matched_path.read_bytes()).hexdigest(),
                'near_original_90_percent':matched['near_original_90_percent'],
                'near_original_90_percent_in_matched_state':matched['near_original_90_percent_in_matched_state'],
                'comparisons':matched['comparisons'],'ordinary':matched['ordinary'],
                'screen_access':False,'normal_exit_verified':False,'gameplay_verified':False}
            evidence['runtime_status']='Matched-resource/profile Present timing and ordinary-launch counters verified. Consult near_original_90_percent; complete gameplay and visual equivalence remain unverified.'
    staged=ROOT/'res/WA2_SDL2_x64.exe'
    for label,name in [('sustained_launcher','sustained-launch-verification.json'),('sustained_standalone','sustained-standalone-verification.json')]:
        path=ROOT/'logs'/name
        if not path.exists():continue
        proof=json.loads(path.read_text())
        if proof.get('output_sha256')!=evidence['output_sha256']:continue
        for key in ['launch_report','runtime_report','menu_screenshot','native_reference']:
            item=proof[key]
            if hashlib.sha256(Path(item['file']).read_bytes()).hexdigest()!=item['sha256']:
                raise ValueError('Sustained-launch evidence changed: '+item['file'])
        evidence[label]={'report':str(path),'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
                         'sustained_30fps':proof['sustained_30fps'],'sampled_rates':proof['sampled_rates'],
                         'precise_present_intervals':proof['precise_present_intervals']}
        evidence['runtime_status']='Visible menu and sustained presentation measured against the native reference. Consult sustained_30fps; a matching screenshot does not establish smooth gameplay.'
    if staged.exists():evidence['resource_directory_executable']={'path':str(staged),'sha256':hashlib.sha256(staged.read_bytes()).hexdigest(),'matches_output':hashlib.sha256(staged.read_bytes()).hexdigest()==evidence['output_sha256']}
    resource_path=ROOT/'logs/resource-lifetime-verification.json'
    if resource_path.exists():
        repair=json.loads(resource_path.read_text())
        if repair.get('output_sha256')==evidence['output_sha256']:
            from verify_resource_repair import verification
            if verification()!=repair:raise ValueError('Resource/heap/package evidence changed')
            evidence['resource_repair']={'report':str(resource_path),'sha256':hashlib.sha256(resource_path.read_bytes()).hexdigest(),
                'ordinary_seconds':repair['ordinary']['seconds'],'actual_fast_forward_verified':False,'package':repair['package']}
            evidence['runtime_status']='Heap/resource churn regressions and a five-minute ordinary launch were verified. Actual fast-forward gameplay, visual equivalence and normal exit remain unverified.'
    current_fix_path=ROOT/'logs/crash-loading-package-verification.json'
    if current_fix_path.exists():
        current= json.loads(current_fix_path.read_text())
        if current.get('output_sha256')==evidence['output_sha256']:
            from verify_crash_loading_package import verification
            if verification()!=current:raise ValueError('Crash, PAK loading or compact-package evidence changed')
            evidence['crash_loading_package']={'report':str(current_fix_path),'sha256':hashlib.sha256(current_fix_path.read_bytes()).hexdigest(),
                'repaired_target':current['repaired_target'],'pak_loads':current['pak_loading']['actual_pak_loads'],
                'actual_game_audio':current['actual_game_audio'],
                'package':current['package'],'actual_gameplay_fast_forward_verified':False,'screen_access':False}
            evidence['runtime_status']='Observed callback and 52,992,000-byte sound lock crashes repaired. Actual large sound resources, native PCM streaming, resource allocation and PAK regressions passed. Full gameplay fast-forward, complete engine reconstruction and all-platform portability remain unverified or incomplete; package remains larger than the original.'
    native_engine_path=ROOT/'logs/native-engine-verification.json'
    if native_engine_path.exists():
        native_engine=json.loads(native_engine_path.read_text())
        if native_engine.get('output_sha256')==evidence['output_sha256']:
            from verify_native_engine import verification
            if verification()!=native_engine:raise ValueError('Native engine or platform evidence changed')
            evidence['native_engine_modules']={'report':str(native_engine_path),'sha256':hashlib.sha256(native_engine_path.read_bytes()).hexdigest(),
                'native_source_functions':native_engine['source_replaced_functions'],'package':native_engine['package'],
                'actual_native_resource_counters':native_engine['actual_native_resource_counters'],
                'android_device_verified':False,'entire_engine_reconstructed':False,'all_platform_apis_portable':False}
            evidence['runtime_status']='Native C++ audio, resource loading, file I/O and clock modules verified, including an ordinary launch without screen access. Other game logic retains checked CPU-state translation and Windows services; complete gameplay, original performance and Android runtime are not verified.'
    (ROOT/'out'/'build-info.json').write_text(json.dumps(evidence,indent=2)+'\n')
    print(json.dumps(evidence,indent=2))

if __name__=='__main__':main()
