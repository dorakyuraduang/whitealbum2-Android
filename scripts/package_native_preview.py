"""Assemble a separate native-development package from audited real hosts.

This keeps the retained AOT application and release metadata intact. It never
installs/launches the package, loads a library or reads the screen. Original
PAKs/fonts/videos stay external; a build is not whole-game verification.
"""
import argparse
import ctypes
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import xml.etree.ElementTree as ET
import zipfile
from build_native_hosts import ROOT,digest,fresh,native_ui,codecs
from package_native_relink import package as package_relink
from verify_shared_package import verify_resource_table

NS='http://schemas.android.com/apk/res/android'
PACKAGE='org.wa2.port.nativeengine'
VERSION='0.3.1-native-development'
VERSION_CODE=2

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--check-only',action='store_true');args=parser.parse_args()
    if os.name!='nt':raise SystemExit('This assembler uses the configured Windows Android SDK/JDK/NDK tools')
    kernel=ctypes.WinDLL('kernel32',use_last_error=True);kernel.GetCurrentProcess.restype=ctypes.c_void_p
    kernel.SetPriorityClass.argtypes=[ctypes.c_void_p,ctypes.c_uint]
    kernel.GetProcessAffinityMask.argtypes=[ctypes.c_void_p,ctypes.POINTER(ctypes.c_size_t),ctypes.POINTER(ctypes.c_size_t)]
    kernel.SetProcessAffinityMask.argtypes=[ctypes.c_void_p,ctypes.c_size_t]
    current=ctypes.c_size_t();system=ctypes.c_size_t();process=kernel.GetCurrentProcess()
    if not kernel.SetPriorityClass(process,0x4000) or not kernel.GetProcessAffinityMask(process,ctypes.byref(current),ctypes.byref(system)) or not kernel.SetProcessAffinityMask(process,current.value&-current.value):raise ctypes.WinError()
    flags=subprocess.CREATE_NO_WINDOW|subprocess.BELOW_NORMAL_PRIORITY_CLASS
    core_path=ROOT/'logs/native-core-verification.json';core=fresh(core_path);codec_bindings=codecs(core)
    production={host:fresh(ROOT/('logs/native-production-'+host+'.json')) for host in ('windows','android')}
    ui_path=ROOT/'logs/native-android-ui.json';ui=native_ui(ui_path)
    for host,report in production.items():
        if not report['actual_application_built'] or not report['production_sdl_backends'] or report['recompiled_game_linked'] or report['guest_cpu_and_memory_linked']:raise ValueError('A real native production host is required: '+host)
        if report['native_core_report_sha256']!=digest(core_path) or report['common_game_sources']!=core['native_compiled_sources']:raise ValueError('Production host does not bind the current common game: '+host)
        if digest(Path(report['application']['file']))!=report['application']['sha256']:raise ValueError('Production artifact changed: '+host)
    sdl=production['android']['sdl']
    if not sdl or digest(Path(sdl['file']))!=sdl['sha256']:raise ValueError('The actual native SDL library changed')
    if args.check_only:
        print('PASS actual native host and Java package prerequisites; no package or app started.');return
    sdk=ROOT/'tools/android-sdk';bt=sdk/'build-tools/34.0.0';ndk=sdk/'ndk/27.0.12077973'
    llvm=ndk/'toolchains/llvm/prebuilt/windows-x86_64';jdk=ROOT/'tools/source-analysis/jdk-21.0.12.1+1'
    api=ROOT/'tools/android-api-35/android.jar'
    if digest(api)!=ui['api_jar_sha256']:raise ValueError('The verified Android API jar changed')
    key=ROOT/'private/android-signing/wa2-native-dev.p12'
    if not key.is_file():key=ROOT/'build/android-signing/wa2-native-dev.p12'
    if not key.is_file():raise ValueError('Existing development signing identity is missing; no identity was created')
    directory=ROOT/'build/native-preview-package'/digest(core_path)[:16];directory.mkdir(parents=True,exist_ok=True)
    output=ROOT/'out/native-development';output.mkdir(parents=True,exist_ok=True)
    bindings={Path(__file__).relative_to(ROOT).as_posix():digest(Path(__file__))}
    for path in (ROOT/'scripts/verify_shared_package.py',ROOT/'third_party/stb/stb_vorbis.c',
            ROOT/'scripts/package_native_relink.py',ROOT/'scripts/native_relink.py',ROOT/'scripts/build_media_codec.py'):
        bindings[path.relative_to(ROOT).as_posix()]=digest(path)
    for report in production.values():bindings.update(report['source_sha256'])
    bindings.update(ui['source_sha256'])
    for path in (ROOT/'android/app/src/main/res').rglob('*'):
        if path.is_file():bindings[path.relative_to(ROOT).as_posix()]=digest(path)
    manifest_source=ROOT/'android/app/src/main/AndroidManifest.xml';bindings[manifest_source.relative_to(ROOT).as_posix()]=digest(manifest_source)
    env=os.environ.copy();env['WA2_NATIVE_SIGNING_PASSWORD']=env.get('WA2_NATIVE_SIGNING_PASSWORD','android')
    env['CMAKE_BUILD_PARALLEL_LEVEL']='1'
    env['ZIG_GLOBAL_CACHE_DIR']=str(ROOT/'build/test-content-046/zig-cache')
    env['ZIG_LOCAL_CACHE_DIR']=str(ROOT/'build/native-desktop-definition/zig-local')
    with (ROOT/'logs/native-preview-package.log').open('wb') as log:
        def run(command):
            command=list(map(str,command));log.write(('RUN '+subprocess.list2cmdline(command)+'\n').encode());log.flush()
            with subprocess.Popen(command,cwd=ROOT,env=env,creationflags=flags,stdout=log,stderr=subprocess.STDOUT) as child:
                try:status=child.wait(timeout=600)
                except subprocess.TimeoutExpired:
                    subprocess.run(['taskkill','/PID',str(child.pid),'/T','/F'],creationflags=flags,stdout=log,stderr=subprocess.STDOUT,timeout=20)
                    child.wait();raise
                if status:raise subprocess.CalledProcessError(status,command)
        def inspect(command):return subprocess.run(list(map(str,command)),cwd=ROOT,env=env,creationflags=flags,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=120,check=True).stdout
        native={
            'libwa2_native_android.so':Path(production['android']['application']['file']),
            'libSDL2.so':Path(sdl['file']),
            'libc++_shared.so':llvm/'sysroot/usr/lib/aarch64-linux-android/libc++_shared.so'}
        libraries={};library_records={}
        for name,path in native.items():
            destination=directory/name;run([llvm/'bin/llvm-strip.exe','--strip-all','-o',destination,path])
            header=inspect([llvm/'bin/llvm-readelf.exe','-h','-lW','-d',destination])
            if 'AArch64' not in header:raise ValueError('Wrong packaged ELF architecture: '+name)
            loads=[line.split() for line in header.splitlines() if line.strip().startswith('LOAD ')]
            if not loads or any(int(fields[-1],16)<16384 for fields in loads):raise ValueError('Native library lacks 16 KiB load alignment: '+name)
            dependencies=re.findall(r'\(NEEDED\).*?\[([^\]]+)\]',header);sonames=re.findall(r'\(SONAME\).*?\[([^\]]+)\]',header)
            if sonames!=[name]:raise ValueError('Native library SONAME does not match its package entry: '+name)
            system={'libandroid.so','libc.so','libdl.so','liblog.so','libm.so','libOpenSLES.so','libGLESv1_CM.so','libGLESv2.so','libEGL.so','libaaudio.so','libvulkan.so'}
            if set(dependencies)-set(native)-system:raise ValueError('Unpackaged Android native dependency for '+name+': '+str(sorted(set(dependencies)-set(native)-system)))
            libraries[name]=destination;library_records[name]=dict(source_sha256=digest(path),sha256=digest(destination),bytes=destination.stat().st_size,load_alignment_at_least_16k=True,needed=dependencies,soname=sonames[0])
        exports=inspect([llvm/'bin/llvm-nm.exe','--dynamic','--defined-only',libraries['libwa2_native_android.so']])
        if not all(re.search(r'\b'+symbol+r'\s*$',exports,re.M) for symbol in ('JNI_OnLoad','SDL_main')):raise ValueError('Native package stripping lost its Java/SDL entry exports')
        windows=directory/'WA2_Native_x64.exe';run([llvm/'bin/llvm-strip.exe','--strip-all','-o',windows,production['windows']['application']['file']])
        manifest=ET.parse(manifest_source);top=manifest.getroot();top.set('package',PACKAGE)
        top.set('{'+NS+'}versionCode',str(VERSION_CODE));top.set('{'+NS+'}versionName',VERSION)
        uses=ET.Element('uses-sdk');uses.set('{'+NS+'}minSdkVersion','26');uses.set('{'+NS+'}targetSdkVersion','35');top.insert(0,uses)
        for feature in top.findall('uses-feature'):
            if feature.get('{'+NS+'}glEsVersion') is not None:feature.set('{'+NS+'}glEsVersion','0x00020000')
        app=top.find('application');app.set('{'+NS+'}label','WA2 原生开发版');app.set('{'+NS+'}extractNativeLibs','true');app.set('{'+NS+'}debuggable','false')
        for activity in app.findall('activity'):
            name=activity.get('{'+NS+'}name','')
            if name.startswith('.'):activity.set('{'+NS+'}name','org.wa2.port'+name)
        ET.register_namespace('android',NS);manifest_path=directory/'AndroidManifest.xml';manifest.write(manifest_path,encoding='utf-8',xml_declaration=True)
        compiled=directory/'compiled-resources.zip';resources=directory/'resources.apk'
        run([bt/'aapt2.exe','compile','--no-crunch','--dir',ROOT/'android/app/src/main/res','-o',compiled])
        run([bt/'aapt2.exe','link','--manifest',manifest_path,'-I',api,compiled,'-o',resources])
        notices=[(ROOT/'LICENSE','MIT.txt'),(ROOT/'NOTICE.md','NOTICE.md'),(ROOT/'THIRD_PARTY_NOTICES.md','THIRD_PARTY_NOTICES.md'),
            (ROOT/'tools/SDL-release-2.32.10/LICENSE.txt','SDL2-LICENSE.txt'),(ROOT/'third_party/ffmpeg/COPYING.LGPLv2.1','FFmpeg-LGPL-2.1.txt'),(ROOT/'third_party/ffmpeg/NOTICE.txt','FFmpeg-NOTICE.txt')]
        for name in ('LICENSE','FONT-NOTICE.txt','README.md','source.json'):notices.append((ROOT/'third_party/wa2_subtitles'/name,'wa2-subtitles/'+name))
        for path,_ in notices:bindings[path.relative_to(ROOT).as_posix()]=digest(path)
        unsigned=directory/'unsigned.apk'
        with zipfile.ZipFile(resources) as resource_zip,zipfile.ZipFile(unsigned,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as package:
            for item in resource_zip.infolist():package.writestr(item.filename,resource_zip.read(item),compress_type=zipfile.ZIP_STORED if item.filename=='resources.arsc' else item.compress_type)
            package.write(ui['native_dex_file'],'classes.dex')
            for name,path in libraries.items():package.write(path,'lib/arm64-v8a/'+name)
            for path,name in notices:package.write(path,'assets/licenses/'+name)
            vorbis=(ROOT/'third_party/stb/stb_vorbis.c').read_text(errors='replace');package.writestr('assets/licenses/stb-vorbis-LICENSE.txt',vorbis[vorbis.rfind('ALTERNATIVE A -'):])
        aligned=directory/'aligned.apk';signed=directory/'WA2-native-arm64-development.apk'
        run([bt/'zipalign.exe','-f','4',unsigned,aligned])
        run([jdk/'bin/java.exe','-XX:ActiveProcessorCount=1','-Xmx256m','-jar',bt/'lib/apksigner.jar','sign','--ks',key,'--ks-key-alias','wa2-dev',
            '--ks-pass','env:WA2_NATIVE_SIGNING_PASSWORD','--key-pass','env:WA2_NATIVE_SIGNING_PASSWORD','--out',signed,aligned])
        run([jdk/'bin/java.exe','-XX:ActiveProcessorCount=1','-Xmx256m','-jar',bt/'lib/apksigner.jar','verify','--verbose','--print-certs',signed]);run([bt/'zipalign.exe','-c','4',signed])
        table=verify_resource_table(signed)
        with zipfile.ZipFile(signed) as package:
            names=package.namelist()
            if len(names)!=len(set(names)) or package.testzip() is not None:raise ValueError('Damaged/duplicate APK entries')
            if any(name.lower().endswith(('.pak','.exe','.tga','.bmp','.ogg','.wmv')) for name in names):raise ValueError('Original game resources in native APK')
            if {name for name in names if name.endswith('.so')}!={'lib/arm64-v8a/'+name for name in libraries}:raise ValueError('Unexpected native APK library set')
            if digest(Path(ui['native_dex_file']))!=ui['native_dex_sha256'] or package.read('classes.dex')!=Path(ui['native_dex_file']).read_bytes():raise ValueError('Native Java bytecode changed')
            for name,path in libraries.items():
                if package.read('lib/arm64-v8a/'+name)!=path.read_bytes():raise ValueError('Packaged native library changed: '+name)
        badging=inspect([bt/'aapt.exe','dump','badging',signed])
        if PACKAGE not in badging or VERSION not in badging or f"versionCode='{VERSION_CODE}'" not in badging or "native-code: 'arm64-v8a'" not in badging or 'org.wa2.port.LauncherActivity' not in badging:raise ValueError('Native APK manifest/entry identity is wrong')
        windows_zip=directory/'WA2-native-Windows-x64-development.zip'
        with zipfile.ZipFile(windows_zip,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as archive:
            archive.write(windows,'WA2_Native_x64.exe')
            for path,name in notices:archive.write(path,'licenses/'+name)
            archive.writestr('licenses/stb-vorbis-LICENSE.txt',vorbis[vorbis.rfind('ALTERNATIVE A -'):])
        print('Checking matching native-engine LGPL relink materials; one hidden job, no application starts.',flush=True)
        relink,relink_manifest=package_relink(directory,dict(android=dict(apk_sha256=digest(signed),library_sha256=digest(libraries['libwa2_native_android.so'])),
            windows=dict(archive_sha256=digest(windows_zip),executable_sha256=digest(windows))),run,inspect)
        for name,value in bindings.items():
            if digest(ROOT/name)!=value:raise ValueError('Native package input changed: '+name)
        fresh(core_path);native_ui(ui_path);codecs(core)
        for host,report in production.items():
            fresh(ROOT/('logs/native-production-'+host+'.json'))
            if digest(Path(report['application']['file']))!=report['application']['sha256']:raise ValueError('Native production artifact changed during packaging: '+host)
        if digest(Path(sdl['file']))!=sdl['sha256']:raise ValueError('Native production SDL changed during packaging')
        artifacts={}
        for host,source in (('android',signed),('windows',windows_zip),('relink',relink)):
            destination=output/source.name;staged=destination.with_suffix(destination.suffix+'.new');shutil.copy2(source,staged);os.replace(staged,destination)
            artifacts[host]=dict(file=str(destination),sha256=digest(destination),bytes=destination.stat().st_size)
        report=dict(schema='wa2.native-preview-package.v1',source_sha256=bindings,codec_archives_sha256=codec_bindings,native_core_report_sha256=digest(core_path),
            production_reports={host:digest(ROOT/('logs/native-production-'+host+'.json')) for host in production},
            artifacts=artifacts,libraries=library_records,application_id=PACKAGE,version=VERSION,version_code=VERSION_CODE,resource_table=table,
            matching_lgpl_relink_materials=True,relink_recipe_verified=relink_manifest['relink_recipe_verified'],
            common_game_sources=core['native_compiled_sources'],native_java_report_sha256=digest(ui_path),
            game_resources_embedded=False,recompiled_game_linked=False,guest_cpu_and_memory_linked=False,
            retained_release_replaced=False,source_executable_opened=False,app_installed=False,game_launched=False,
            screen_access=False,android_device_verified=False,whole_game_verified=False,complete_game_claim=False,passed=True)
        (ROOT/'logs/native-preview-package.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print('PASS separate native development APK/desktop archive from actual hosts; no app installed or launched. Whole-game compatibility remains unverified.')

if __name__=='__main__':main()
