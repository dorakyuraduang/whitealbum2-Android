"""Audit the native Android build definition and compiled Java/JNI interface.

This configures full production SDL sources but never builds/loads/launches an
Android app. It requires fresh finite native-core and Java-only checks first.
"""
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
from verify_sdl_jni import verify as verify_sdl_jni

ROOT=Path(__file__).resolve().parents[1]
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def fresh(path):
    report=json.loads(path.read_text())
    if not report.get('passed'):raise ValueError('A passing report is required: '+str(path))
    for name,value in report['source_sha256'].items():
        if digest(ROOT/name)!=value:raise ValueError('Changed verification input: '+name)
    return report

def main():
    if os.name!='nt':raise SystemExit('This verifier uses the existing Windows Android NDK/JDK tools')
    kernel=ctypes.WinDLL('kernel32',use_last_error=True);kernel.GetCurrentProcess.restype=ctypes.c_void_p
    kernel.SetPriorityClass.argtypes=[ctypes.c_void_p,ctypes.c_uint];kernel.GetProcessAffinityMask.argtypes=[ctypes.c_void_p,ctypes.POINTER(ctypes.c_size_t),ctypes.POINTER(ctypes.c_size_t)];kernel.SetProcessAffinityMask.argtypes=[ctypes.c_void_p,ctypes.c_size_t]
    process=kernel.GetCurrentProcess();current=ctypes.c_size_t();system=ctypes.c_size_t()
    if not kernel.SetPriorityClass(process,0x4000) or not kernel.GetProcessAffinityMask(process,ctypes.byref(current),ctypes.byref(system)) or not kernel.SetProcessAffinityMask(process,current.value&-current.value):raise ctypes.WinError()
    core_path=ROOT/'logs/native-core-verification.json';ui_path=ROOT/'logs/native-android-ui.json';core=fresh(core_path);ui=fresh(ui_path)
    entry=core['android_native_entry'];library=Path(entry['file'])
    if not entry['symbol_and_dynamic_dependencies_audited'] or digest(library)!=entry['sha256']:raise ValueError('A fresh actual native JNI entry is required')
    native=ui['source_sets']['native'];classes=Path(native['classes_directory'])
    for name,value in native['classes_sha256'].items():
        if digest(classes/name)!=value:raise ValueError('Native Java class changed: '+name)
    for file,key in (('native_dex_file','native_dex_sha256'),('native_ui_jar_file','native_ui_jar_sha256')):
        if digest(Path(ui[file]))!=ui[key]:raise ValueError('Native Java artifact changed: '+file)
    inputs={**core['source_sha256'],**ui['source_sha256']}
    for path in [Path(__file__),ROOT/'scripts/verify_sdl_jni.py',ROOT/'tools/SDL-release-2.32.10/CMakeLists.txt',*sorted((ROOT/'tools/SDL-release-2.32.10/cmake').rglob('*.cmake'))]:inputs[path.relative_to(ROOT).as_posix()]=digest(path)
    flags=subprocess.CREATE_NO_WINDOW|subprocess.BELOW_NORMAL_PRIORITY_CLASS
    def output(command,env=None,timeout=60):return subprocess.run(list(map(str,command)),cwd=ROOT,env=env,creationflags=flags,capture_output=True,text=True,encoding='utf-8',errors='replace',check=True,timeout=timeout).stdout
    jdk=ROOT/'tools/source-analysis/jdk-21.0.12.1+1';declarations=output([jdk/'bin/javap.exe','-p','-s',classes/'org/wa2/port/PortUi.class'])
    expected={'nativeLabels':'()[Ljava/lang/String;','nativeState':'()J','nativeChoose':'(I)Z'};methods={};pending=None
    for line in declarations.splitlines():
        found=re.search(r'\bnative\b.*\s(\w+)\(',line)
        if found:pending=found[1]
        descriptor=re.match(r'\s+descriptor:\s+(\(.*\).+)$',line)
        if pending and descriptor:methods[pending]=descriptor[1];pending=None
    if methods!=expected or pending:raise ValueError('Native Java/JNI menu declarations do not match the native service')
    data=library.read_bytes()
    if b'LandscapeLeft LandscapeRight\0' not in data:raise ValueError('Linked native Android entry lacks its landscape-only SDL orientation hint')
    for value in ['org/wa2/port/PortUi',*expected,*expected.values()]:
        if value.encode() not in data:raise ValueError('Linked native JNI service lacks registration data: '+value)
    declarations=output([jdk/'bin/javap.exe','-p','-s',classes/'org/wa2/port/ResourceDirectory.class'])
    resource_expected={'openPak':'(Ljava/lang/String;)I','openOptionalPak':'(Ljava/lang/String;)I',
        'openLegacySettings':'(Ljava/lang/String;)I','openLegacySave':'(Ljava/lang/String;)I','legacySaveMetadata':'()[Ljava/lang/String;',
        'gameStorageToken':'()I','openGameFile':'(Ljava/lang/String;I)I','replaceGameFile':'(Ljava/lang/String;ILjava/nio/ByteBuffer;)V',
        'updateGameFile':'(Ljava/lang/String;IJJLjava/nio/ByteBuffer;)V','gameSaveMetadata':'(ILjava/lang/String;)[Ljava/lang/String;',
        'listPaks':'()[Ljava/lang/String;','pakMetadata':'(Ljava/lang/String;)[J','reportFailure':'(Ljava/lang/String;)V'}
    resource_methods={};pending=None
    for line in declarations.splitlines():
        found=re.search(r'\bpublic static\b.*\s(\w+)\(',line)
        if found:pending=found[1] if found[1] in resource_expected else None
        descriptor=re.match(r'\s+descriptor:\s+(\(.*\).+)$',line)
        if pending and descriptor:resource_methods[pending]=descriptor[1];pending=None
    if resource_methods!=resource_expected or pending:raise ValueError('Native Java resource/settings descriptors do not match the native service')
    for value in ['org/wa2/port/ResourceDirectory',*resource_expected,*resource_expected.values()]:
        if value.encode() not in data:raise ValueError('Linked native resource/settings JNI lacks registration data: '+value)
    sdl=ROOT/'build/test-content-046/libSDL2.so'
    if digest(sdl)!=core['android_sdl_library_sha256']:raise ValueError('Verified SDL library changed')
    sdl_jni=verify_sdl_jni(classes,sdl)
    directory=ROOT/'build/native-android-definition';directory.mkdir(parents=True,exist_ok=True);env=os.environ.copy();env['CMAKE_BUILD_PARALLEL_LEVEL']='1'
    cmake=ROOT/'tools/python/cmake/data/bin/cmake.exe';ninja=ROOT/'tools/python/bin/ninja.exe';ndk=ROOT/'tools/android-sdk/ndk/27.0.12077973'
    command=[cmake,'-S',ROOT/'hosts/native-android','-B',directory,'-G','Ninja','-DCMAKE_TOOLCHAIN_FILE='+str(ndk/'build/cmake/android.toolchain.cmake'),
        '-DANDROID_ABI=arm64-v8a','-DANDROID_PLATFORM=android-26','-DANDROID_STL=c++_shared','-DCMAKE_BUILD_TYPE=MinSizeRel','-DCMAKE_EXPORT_COMPILE_COMMANDS=ON','-DCMAKE_MAKE_PROGRAM='+str(ninja)]
    print('Configuring full native Android SDL root; one hidden low-priority job. No app or device starts.',flush=True)
    with (ROOT/'logs/native-android-definition.log').open('wb') as log:subprocess.run(list(map(str,command)),cwd=ROOT,env=env,creationflags=flags,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=600)
    graph=output([ninja,'-C',directory,'-t','graph','libwa2_native_android.so'],env).replace('\\','/').lower()
    for token in ('recompiled/','generated/','winrecomp','wa2_function_process','wa2_shared_aot','/port/'):
        if token in graph:raise ValueError('Legacy dependency in full native Android graph: '+token)
    commands=json.loads((directory/'compile_commands.json').read_text());sources=sorted({Path(item['file']).resolve().relative_to(ROOT).as_posix() for item in commands if Path(item['output']).resolve().relative_to(directory).as_posix().lower() in graph})
    expected_common=set(core['native_compiled_sources'])
    if not expected_common.issubset(sources):raise ValueError('Full native Android root omits common sources: '+str(expected_common-set(sources)))
    for name in ('android/game/game_host.cpp','android/game/game_jni.cpp','android/game/user_menu_jni.cpp','platform/native_host.cpp',
        'tools/SDL-release-2.32.10/src/core/android/SDL_android.c','tools/SDL-release-2.32.10/src/video/android/SDL_androidvideo.c',
        'tools/SDL-release-2.32.10/src/render/opengles2/SDL_render_gles2.c','tools/SDL-release-2.32.10/src/hidapi/android/hid.cpp'):
        if name not in sources:raise ValueError('Missing full Android platform/SDL source: '+name)
    if not any('/src/audio/aaudio/' in name or '/src/audio/openslES/' in name for name in sources):raise ValueError('Native Android root lacks its device audio backend')
    cache=(directory/'CMakeCache.txt').read_text(encoding='utf-8')
    if any(name+':BOOL=ON' not in cache for name in ('SDL_HIDAPI','SDL_VIDEO','SDL_AUDIO','SDL_RENDER','SDL_OPENGLES')):raise ValueError('Native Android SDL production subsystem disabled')
    for name,value in inputs.items():
        if digest(ROOT/name)!=value:raise ValueError('Native Android definition changed during verification: '+name)
    report=dict(schema='wa2.native-android-definition.v1',source_sha256=inputs,native_core_report_sha256=digest(core_path),java_ui_report_sha256=digest(ui_path),
        actual_native_jni_library_sha256=digest(library),native_menu_java_jni_signatures=methods,native_resource_java_jni_signatures=resource_methods,sdl_java_native_exports=sdl_jni,
        compile_commands_sha256=digest(directory/'compile_commands.json'),link_graph_sha256=hashlib.sha256(graph.encode()).hexdigest(),common_game_sources=len(expected_common),compiled_source_definitions=sources,
        production_sdl_window_gles_audio_hid_sources=True,configured_only=True,game_built=False,library_loaded=False,apk_created=False,android_device_verified=False,game_launched=False,screen_access=False,passed=True)
    (ROOT/'logs/native-android-definition.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print('PASS full native Android source definition, actual linked menu JNI signatures and SDL Java exports; no app launched.')

if __name__=='__main__':main()
