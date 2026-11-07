"""Build and inspect the actual native desktop/Android application targets.

Requires fresh native core, Java and production-source definition checks. No
game, app, library, emulator, graphics device or screen is started. Outputs are
unlaunched migration artifacts, not a claim of whole-game compatibility.
"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[1]

def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def fresh(path):
    report=json.loads(path.read_text(encoding='utf-8'))
    if not report.get('passed') or not report.get('source_sha256'):raise ValueError('A passing source-bound check is required: '+str(path))
    for name,value in report['source_sha256'].items():
        if digest(ROOT/name)!=value:raise ValueError('Changed verification input: '+name)
    return report

def native_ui(path):
    report=fresh(path);native=report['source_sets']['native'];classes=Path(native['classes_directory'])
    for name,value in native['classes_sha256'].items():
        if digest(classes/name)!=value:raise ValueError('Changed native Java class: '+name)
    for file,key in (('native_dex_file','native_dex_sha256'),('native_ui_jar_file','native_ui_jar_sha256')):
        if digest(Path(report[file]))!=report[key]:raise ValueError('Changed native Java artifact: '+file)
    return report

def codecs(report):
    archives=report.get('codec_archives_sha256')
    if not archives:raise ValueError('Source-bound native codec archives are required')
    for name,value in archives.items():
        if digest(ROOT/name)!=value:raise ValueError('Changed native codec archive: '+name)
    return dict(archives)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host',choices=('all','windows','android'),default='all')
    parser.add_argument('--check-only',action='store_true')
    args=parser.parse_args()
    if os.name!='nt':raise SystemExit('This builder uses the configured local Windows Zig/Android NDK toolchains')
    kernel=ctypes.WinDLL('kernel32',use_last_error=True);kernel.GetCurrentProcess.restype=ctypes.c_void_p
    kernel.SetPriorityClass.argtypes=[ctypes.c_void_p,ctypes.c_uint]
    kernel.GetProcessAffinityMask.argtypes=[ctypes.c_void_p,ctypes.POINTER(ctypes.c_size_t),ctypes.POINTER(ctypes.c_size_t)]
    kernel.SetProcessAffinityMask.argtypes=[ctypes.c_void_p,ctypes.c_size_t]
    process=kernel.GetCurrentProcess();current=ctypes.c_size_t();system=ctypes.c_size_t()
    if not kernel.SetPriorityClass(process,0x4000) or not kernel.GetProcessAffinityMask(process,ctypes.byref(current),ctypes.byref(system)) or not kernel.SetProcessAffinityMask(process,current.value&-current.value):raise ctypes.WinError()
    flags=subprocess.CREATE_NO_WINDOW|subprocess.BELOW_NORMAL_PRIORITY_CLASS
    core_path=ROOT/'logs/native-core-verification.json';core=fresh(core_path);codec_bindings=codecs(core)
    hosts=('windows','android') if args.host=='all' else (args.host,)
    reports={host:ROOT/('logs/native-'+('desktop' if host=='windows' else 'android')+'-definition.json') for host in hosts}
    definitions={host:fresh(path) for host,path in reports.items()}
    ui_path=ROOT/'logs/native-android-ui.json'
    if 'android' in hosts:native_ui(ui_path)
    for host,definition in definitions.items():
        if definition['native_core_report_sha256']!=digest(core_path):raise ValueError('The '+host+' definition does not bind the current native core report')
        if host=='android' and definition['java_ui_report_sha256']!=digest(ui_path):raise ValueError('The Android definition does not bind the current Java report')
    if args.check_only:
        print('PASS native production build prerequisites; no build, game or device started.');return
    cmake=ROOT/'tools/python/cmake/data/bin/cmake.exe';ninja=ROOT/'tools/python/bin/ninja.exe'
    llvm=ROOT/'tools/android-sdk/ndk/27.0.12077973/toolchains/llvm/prebuilt/windows-x86_64/bin'
    env=os.environ.copy();env['CMAKE_BUILD_PARALLEL_LEVEL']='1';env['ZIG_GLOBAL_CACHE_DIR']=str(ROOT/'build/test-content-046/zig-cache')
    (ROOT/'build/native-production-temp').mkdir(parents=True,exist_ok=True)
    for name in ('TEMP','TMP'):env[name]=str(ROOT/'build/native-production-temp')
    forbidden=('recompiled/','generated/','winrecomp','wa2_function_process','wa2_shared_aot','/port/')
    for host in hosts:
        directory=ROOT/('build/native-'+('desktop' if host=='windows' else 'android')+'-definition')
        artifact=directory/('WA2_Native_x64.exe' if host=='windows' else 'libwa2_native_android.so')
        target='wa2_native_desktop' if host=='windows' else 'wa2_native_android'
        definition=definitions[host];bindings=dict(definition['source_sha256']);bindings[Path(__file__).relative_to(ROOT).as_posix()]=digest(Path(__file__))
        for name in definition['compiled_source_definitions']:bindings[name]=digest(ROOT/name)
        for folder in ('include','src'):
            for path in (ROOT/'tools/SDL-release-2.32.10'/folder).rglob('*.h'):bindings[path.relative_to(ROOT).as_posix()]=digest(path)
        env['ZIG_LOCAL_CACHE_DIR']=str(directory/'zig-local')
        print('Building full native '+host+' application; one hidden below-normal job on one CPU. No application is started.',flush=True)
        with (ROOT/('logs/native-production-'+host+'.log')).open('wb') as log:
            def run(command):
                command=list(map(str,command));log.write(('RUN '+subprocess.list2cmdline(command)+'\n').encode());log.flush()
                subprocess.run(command,cwd=ROOT,env=env,creationflags=flags,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=3600)
            def output(command):return subprocess.run(list(map(str,command)),cwd=ROOT,env=env,creationflags=flags,capture_output=True,text=True,encoding='utf-8',errors='replace',check=True,timeout=120).stdout
            run([cmake,'--build',directory,'--parallel','1','--target',target])
            graph=output([ninja,'-C',directory,'-t','graph',artifact.name]).replace('\\','/').lower()
            for token in forbidden:
                if token in graph:raise ValueError('Legacy dependency in actual native application: '+token)
            commands=json.loads((directory/'compile_commands.json').read_text(encoding='utf-8'))
            sources=sorted({Path(item['file']).resolve().relative_to(ROOT).as_posix() for item in commands if Path(item['output']).resolve().relative_to(directory).as_posix().lower() in graph})
            if sources!=definition['compiled_source_definitions']:raise ValueError('Actual native production source closure changed')
            if not set(core['native_compiled_sources']).issubset(sources):raise ValueError('Native production application omits common game sources')
            record=dict(file=str(artifact),sha256=digest(artifact),bytes=artifact.stat().st_size)
            sdl_record=None
            if host=='windows':
                header=output([llvm/'llvm-readobj.exe','--file-headers','--coff-imports',artifact])
                if 'IMAGE_FILE_MACHINE_AMD64' not in header or 'IMAGE_SUBSYSTEM_WINDOWS_GUI' not in header:raise ValueError('Native desktop target is not a GUI x64 application')
                if any(token in header.lower() for token in ('winrecomp','wa2_aot','wa2_android')):raise ValueError('Legacy library imported by native desktop application')
            else:
                header=output([llvm/'llvm-readelf.exe','-h',artifact]);symbols=output([llvm/'llvm-nm.exe','--defined-only','--demangle',artifact])
                if 'AArch64' not in header:raise ValueError('Native Android application has the wrong architecture')
                for token in ('JNI_OnLoad','SDL_main','wa2::platform::run_native_host(','wa2::game::Application::frame(','wa2::game::Scene::update('):
                    if token not in symbols:raise ValueError('Missing actual Android application entry/logic: '+token)
                for token in ('wr::','wa2::aot::','code_page_','native_backend_','initialize_widgets_jni','initialize_shell_jni','mount_compiled_resources'):
                    if token in symbols:raise ValueError('Legacy service in native Android application: '+token)
                sdl=directory/'sdl2/libSDL2.so'
                from verify_sdl_jni import verify
                ui=native_ui(ui_path);jni=verify(Path(ui['source_sets']['native']['classes_directory']),sdl)
                sdl_record=dict(file=str(sdl),sha256=digest(sdl),bytes=sdl.stat().st_size,java_native_exports=jni)
            for name,value in bindings.items():
                if digest(ROOT/name)!=value:raise ValueError('Native production source changed during its build: '+name)
            fresh(core_path);fresh(reports[host]);codecs(core)
            report=dict(schema='wa2.native-production-build.v1',host=host,source_sha256=bindings,codec_archives_sha256=codec_bindings,native_core_report_sha256=digest(core_path),
                definition_report_sha256=digest(reports[host]),compile_commands_sha256=digest(directory/'compile_commands.json'),
                link_graph_sha256=hashlib.sha256(graph.encode()).hexdigest(),definition_link_graph_sha256=definition['link_graph_sha256'],compiled_sources=sources,common_game_sources=core['native_compiled_sources'],
                application=record,sdl=sdl_record,production_sdl_backends=True,recompiled_game_linked=False,guest_cpu_and_memory_linked=False,
                actual_application_built=True,artifact_started=False,library_loaded=False,game_launched=False,emulator_launched=False,
                devices_opened=False,screen_access=False,whole_game_verified=False,apk_created=False,passed=True)
            (ROOT/('logs/native-production-'+host+'.json')).write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
        print('PASS actual native '+host+' build and dependency audit; no application or device started.',flush=True)

if __name__=='__main__':main()
