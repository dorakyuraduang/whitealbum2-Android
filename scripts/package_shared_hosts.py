"""Build and package the common static-recompiled hosts, without running them.

One hidden below-normal build job. Original PAKs stay external; no original
executable, game process, emulator, screen access or graphics device is used.
"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
import xml.etree.ElementTree as ET
import zipfile
from package_shared_relink import package as package_relink

ROOT=Path(__file__).resolve().parents[1]
NS='http://schemas.android.com/apk/res/android'


def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def publish_file(source,destination):
    """Publish verified bytes; preserve a mapped Windows EXE without killing it."""
    source=Path(source).resolve();destination=Path(destination).resolve()
    if not destination.is_relative_to(ROOT.resolve()):raise ValueError('Publish target is outside the workspace')
    staged=destination.with_name(destination.name+'.new-'+digest(source)[:16])
    shutil.copy2(source,staged)
    try:os.replace(staged,destination)
    except PermissionError:
        if destination.name!='WA2_SDL2_x64.exe' or not destination.is_file():raise
        archive=(ROOT/'build/retired-deliveries').resolve()
        if not archive.is_relative_to(ROOT.resolve()):raise ValueError('Retired delivery directory is outside the workspace')
        archive.mkdir(parents=True,exist_ok=True)
        retired=archive/(destination.stem+'-'+digest(destination)[:16]+'-'+str(time.time_ns())+'.exe')
        # Windows permits renaming a loaded executable even when overwriting
        # it is denied. Keep the old file until its owner exits naturally.
        destination.rename(retired)
        try:os.replace(staged,destination)
        except OSError:
            if not destination.exists():retired.rename(destination)
            raise
        print('Preserved an in-use previous Windows EXE at '+str(retired),flush=True)


def fresh(path):
    proof=json.loads(path.read_text())
    if not proof.get('passed') or not proof.get('source_sha256'):raise ValueError('Missing verification: '+str(path))
    for name,sha in proof['source_sha256'].items():
        if digest(ROOT/name)!=sha:raise ValueError('Stale verification input: '+name)
    return proof


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check-only',action='store_true');args=parser.parse_args()
    kernel=ctypes.WinDLL('kernel32',use_last_error=True);kernel.GetCurrentProcess.restype=ctypes.c_void_p
    kernel.SetPriorityClass.argtypes=[ctypes.c_void_p,ctypes.c_uint]
    if not kernel.SetPriorityClass(kernel.GetCurrentProcess(),0x4000):raise ctypes.WinError(ctypes.get_last_error())
    flags=subprocess.BELOW_NORMAL_PRIORITY_CLASS|subprocess.CREATE_NO_WINDOW
    sdk=ROOT/'tools/android-sdk';ndk=sdk/'ndk/27.0.12077973';bt=sdk/'build-tools/34.0.0'
    jdk=ROOT/'tools/source-analysis/jdk-21.0.12.1+1';api=sdk/'platforms/android-35/android.jar'
    cmake=ROOT/'tools/python/cmake/data/bin/cmake.exe';build=ROOT/'build/shared-host-win64'
    proof_path=ROOT/'logs/shared-aot-verification.json'
    proof=fresh(proof_path);native=Path(proof['actual_android_host']['file'])
    release=json.loads((ROOT/'config/release.json').read_text())
    if digest(native)!=proof['actual_android_host']['sha256']:raise ValueError('Actual Android host changed after verification')
    if args.check_only:
        print('Common AOT checks and actual ARM64 host are current. No game or package was started.');return 0
    env=os.environ.copy();env.update(JAVA_HOME=str(jdk),ANDROID_HOME=str(sdk),ANDROID_SDK_ROOT=str(sdk),
        CMAKE_BUILD_PARALLEL_LEVEL='1',TEMP=str(ROOT/'build/temp'),TMP=str(ROOT/'build/temp'),
        ZIG_GLOBAL_CACHE_DIR=str(ROOT/'build/zig-cache'),ZIG_LOCAL_CACHE_DIR=str(ROOT/'build/zig-local'))
    tracked=[Path(__file__),ROOT/'scripts/package_shared_relink.py',ROOT/'scripts/shared_relink.py',
        ROOT/'scripts/verify_shared_package.py',ROOT/'scripts/verify_sdl_jni.py',ROOT/'tests/apk_install_rules.py',ROOT/'config/release.json',ROOT/'android/app/src/main/AndroidManifest.xml',ROOT/'android/CMakeLists.txt',
        ROOT/'LICENSE',ROOT/'NOTICE.md',ROOT/'THIRD_PARTY_NOTICES.md',
        ROOT/'scripts/package_android.py',ROOT/'scripts/build_android.py',ROOT/'scripts/verify_android_apk.py',
        ROOT/'third_party/ffmpeg/NOTICE.txt',ROOT/'third_party/ffmpeg/COPYING.LGPLv2.1',ROOT/'tools/mojoshader-main/LICENSE.txt',
        ROOT/'tools/mojoshader_assembler-main/LICENSE.txt',ROOT/'tools/SDL-release-2.32.10/LICENSE.txt',ROOT/'third_party/stb/stb_vorbis.c']
    java=sorted((ROOT/'android/app/src/main/java').rglob('*.java'))+sorted((ROOT/'tools/SDL-release-2.32.10/android-project/app/src/main/java').rglob('*.java'))
    tracked+=java;bindings={p.relative_to(ROOT).as_posix():digest(p) for p in tracked}
    identity=hashlib.sha256((digest(proof_path)+json.dumps(bindings,sort_keys=True)).encode()).hexdigest()[:16]
    directory=ROOT/'build/shared-apk'/identity;directory.mkdir(parents=True,exist_ok=True)
    output=ROOT/'out';(output/'android').mkdir(parents=True,exist_ok=True)
    with (ROOT/'logs/shared-host-package.log').open('wb') as console:
        def run(command,timeout=180):
            with subprocess.Popen(list(map(str,command)),cwd=ROOT,env=env,creationflags=flags,stdout=console,stderr=subprocess.STDOUT) as child:
                try:status=child.wait(timeout)
                except subprocess.TimeoutExpired:
                    subprocess.run(['taskkill','/PID',str(child.pid),'/T','/F'],creationflags=flags,stdout=console,stderr=subprocess.STDOUT,timeout=20);child.wait(20);raise
                if status:raise subprocess.CalledProcessError(status,command)
        run([sys.executable,ROOT/'tests/apk_install_rules.py'])
        print('Building the Windows host from the same common game/AOT sources, one below-normal job.',flush=True)
        run([cmake,'-S',ROOT/'hosts','-B',build,'-G','Ninja','-DCMAKE_TOOLCHAIN_FILE='+str(ROOT/'scripts/zig-toolchain.cmake'),
            '-DCMAKE_BUILD_TYPE=MinSizeRel','-DCMAKE_EXPORT_COMPILE_COMMANDS=ON'])
        run([cmake,'--build',build,'--target','wa2_native','--parallel','1'],2400)
        windows=build/'WA2_SDL2_x64.exe'
        from verify_shared_package import common_sources
        if common_sources(build)!=proof['actual_compiled_shared_sources']:raise ValueError('Packaged Windows host used different shared game sources')
        reader=ndk/'toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-readelf.exe';strip=reader.with_name('llvm-strip.exe')
        raw={
            'libwa2_android.so':native,
            'libSDL2.so':ROOT/'build/android-native-game/sdl2/libSDL2.so',
            'libc++_shared.so':ndk/'toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so'}
        libraries={};library_records={}
        for name,path in raw.items():
            stripped=directory/name;run([strip,'--strip-all','-o',stripped,path]);libraries[name]=stripped
            library_records[name]=dict(file=str(path),sha256=digest(path),stripped_file=str(stripped),stripped_sha256=digest(stripped),bytes=stripped.stat().st_size)
        windows_stripped=directory/'WA2_SDL2_x64.exe';run([strip,'--strip-all','-o',windows_stripped,windows])
        print('Compiling Android activity and packaging the actual common ARM64 game library.',flush=True)
        classes=directory/'classes';classes.mkdir(exist_ok=True)
        run([jdk/'bin/javac.exe','-J-XX:ActiveProcessorCount=1','-J-Xmx256m','-encoding','UTF-8','-source','8','-target','8','-parameters','-classpath',api,'-d',classes,*java])
        jar=directory/'classes.jar';run([jdk/'bin/jar.exe','--create','--file',jar,'-C',classes,'.'])
        dex=directory/'dex';dex.mkdir(exist_ok=True)
        run([jdk/'bin/java.exe','-XX:ActiveProcessorCount=1','-Xmx512m','-cp',bt/'lib/d8.jar','com.android.tools.r8.D8','--min-api','26','--lib',api,'--output',dex,jar])
        manifest=ET.parse(ROOT/'android/app/src/main/AndroidManifest.xml');top=manifest.getroot()
        top.set('package','org.wa2.port');top.set('{'+NS+'}versionCode',str(release['code']));top.set('{'+NS+'}versionName',release['name'])
        uses=ET.Element('uses-sdk');uses.set('{'+NS+'}minSdkVersion','26');uses.set('{'+NS+'}targetSdkVersion','35');top.insert(0,uses)
        app=top.find('application');app.set('{'+NS+'}extractNativeLibs','true');app.set('{'+NS+'}debuggable','false')
        ET.register_namespace('android',NS);manifest_path=directory/'AndroidManifest.xml';manifest.write(manifest_path,encoding='utf-8',xml_declaration=True)
        resources=directory/'resources.apk';run([bt/'aapt2.exe','link','--manifest',manifest_path,'-I',api,'-o',resources])
        unsigned=directory/'unsigned.apk'
        with zipfile.ZipFile(resources) as res,zipfile.ZipFile(unsigned,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as apk:
            # Android 11+ rejects a compressed resources.arsc at install time.
            # Preserve aapt2's storage decisions; zipalign aligns stored entries.
            for info in res.infolist():
                compression=zipfile.ZIP_STORED if info.filename=='resources.arsc' else info.compress_type
                apk.writestr(info.filename,res.read(info),compress_type=compression)
            for path in dex.glob('classes*.dex'):apk.write(path,path.name)
            for name,path in libraries.items():apk.write(path,'lib/arm64-v8a/'+name)
            for path,name in ((ROOT/'third_party/ffmpeg/COPYING.LGPLv2.1','FFmpeg-LGPL-2.1.txt'),
                (ROOT/'LICENSE','WA2-Shared-Recomp-MIT.txt'),(ROOT/'NOTICE.md','WA2-Shared-Recomp-NOTICE.md'),
                (ROOT/'THIRD_PARTY_NOTICES.md','THIRD_PARTY_NOTICES.md'),
                (ROOT/'third_party/ffmpeg/NOTICE.txt','FFmpeg-NOTICE.txt'),(ROOT/'tools/SDL-release-2.32.10/LICENSE.txt','SDL2-LICENSE.txt'),
                (ROOT/'tools/mojoshader-main/LICENSE.txt','MojoShader-LICENSE.txt'),
                (ROOT/'tools/mojoshader_assembler-main/LICENSE.txt','MojoShader-assembler-LICENSE.txt')):apk.write(path,'assets/licenses/'+name)
            vorbis=(ROOT/'third_party/stb/stb_vorbis.c').read_text(errors='replace');apk.writestr('assets/licenses/stb-vorbis-LICENSE.txt',vorbis[vorbis.rfind('ALTERNATIVE A -'):])
        aligned=directory/'aligned.apk';run([bt/'zipalign.exe','-f','4',unsigned,aligned])
        key=ROOT/'build/android-signing/wa2-native-dev.p12'
        if not key.is_file():raise ValueError('The existing development signing identity is missing')
        signed=directory/'WA2-arm64-dev.apk'
        run([jdk/'bin/java.exe','-XX:ActiveProcessorCount=1','-Xmx256m','-jar',bt/'lib/apksigner.jar','sign','--ks',key,
            '--ks-key-alias','wa2-dev','--ks-pass','pass:android','--key-pass','pass:android','--out',signed,aligned])
        run([jdk/'bin/java.exe','-XX:ActiveProcessorCount=1','-Xmx256m','-jar',bt/'lib/apksigner.jar','verify','--verbose','--print-certs',signed])
        run([bt/'zipalign.exe','-c','4',signed])
        for name,sha in bindings.items():
            if digest(ROOT/name)!=sha:raise ValueError('Package input changed: '+name)
        fresh(proof_path)
        artifacts=dict(android=dict(file=str(signed),sha256=digest(signed),bytes=signed.stat().st_size),
            windows=dict(file=str(windows_stripped),sha256=digest(windows_stripped),bytes=windows_stripped.stat().st_size))
        print('Checking replaceable codec relink materials for both hosts, without launching either.',flush=True)
        relink=package_relink(artifacts,console,env,flags)
        status=dict(schema='wa2.shared-host-package.v1',source_sha256=bindings,verification_sha256=digest(proof_path),
            verification_report=str(proof_path),libraries=library_records,artifacts=artifacts,relink_materials=str(relink),relink_sha256=digest(relink),
            one_game_source_set=True,legacy_scene_host_linked=False,runtime_instruction_decoder_linked=False,jit_linked=False,
            source_abi_register_state_retained=True,primary_storage_resource_directory='Wa2Res',original_resources_in_apk=False,
            android_launcher_settings=True,android_local_wa2res_selection=True,android_fps_preference=True,
            native_math_function_ports=[item['name'] for item in json.loads((ROOT/'generated/shared-aot/manifest.json').read_text())['native_function_ports']],native_math_checks=proof['crt_math_paths'],
            cached_source_initial_data_included=True,requested_refresh_hz=120,measured_fps=None,android_device_verified=False,game_launched=False,screen_access=False,
            complete_gameplay_verified=False,partially_admitted_entries=proof['partially_admitted_entries'],
            unsupported_instruction_sites=proof['unsupported_instruction_sites'],maximum_build_jobs=1,passed=True)
        from verify_shared_package import verify
        verify(status,publish=False)
        destinations={'android':output/'android/WA2-arm64-dev.apk','windows':output/'WA2_SDL2_x64.exe'}
        for host,path in destinations.items():
            publish_file(artifacts[host]['file'],path);artifacts[host]['file']=str(path)
        resource_copy=ROOT/'res/WA2_SDL2_x64.exe'
        if resource_copy.is_file():publish_file(windows_stripped,resource_copy)
        status['artifacts']=artifacts
        (ROOT/'logs/shared-host-package.json').write_text(json.dumps(status,indent=2)+'\n')
        (ROOT/'logs/android-build-status.json').write_text(json.dumps(status,indent=2)+'\n')
        (output/'build-info.json').write_text(json.dumps(status,indent=2)+'\n')
        verify(status,publish=True)
        old_relink=output/'android/WA2-native-relink.zip'
        if old_relink.is_file():
            archive_root=(ROOT/'build/retired-deliveries').resolve()
            if not archive_root.is_relative_to(ROOT.resolve()):raise ValueError('Retired delivery directory is outside the workspace')
            archive_root.mkdir(parents=True,exist_ok=True)
            old_target=archive_root/('WA2-native-relink-'+digest(old_relink)[:16]+'.zip')
            if old_target.exists():
                if digest(old_target)!=digest(old_relink):raise ValueError('Conflicting retired relink archive')
                old_relink.unlink()
            else:shutil.move(old_relink,old_target)
        print('Packaged the common Windows x64 and Android ARM64 hosts. Device/gameplay verification has not been performed.',flush=True)
    return 0


if __name__=='__main__':raise SystemExit(main())
