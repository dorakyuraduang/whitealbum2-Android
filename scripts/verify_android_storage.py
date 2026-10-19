"""Check direct external-PAK access without the game, windows, or full extraction."""
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[1]


def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    directory=ROOT/'build/android-storage';directory.mkdir(parents=True,exist_ok=True)
    jdk=ROOT/'tools/source-analysis/jdk-21.0.12.1+1'
    zig=ROOT/'tools/python/ziglang/zig.exe'
    inputs=[ROOT/p for p in ('platform/resource_fd.hpp','platform/resource_fd.cpp','platform/file_io.cpp',
        'engine/resource_archive.hpp','engine/resource_archive.cpp','tests/resource_descriptors.cpp',
        'android/native/android_resources.hpp','android/native/android_resources.cpp','android/native/resource_jni.hpp',
        'android/native/frame_metrics_jni.cpp','android/CMakeLists.txt','platform/portable.cmake',
        'tests/android/org/wa2/port/ArchiveNamesTest.java','tests/android/org/wa2/port/ResourceLocationTest.java',
        'scripts/verify_android_storage.py')]
    inputs+=list((ROOT/'android/app/src/main/java/org/wa2/port').glob('*.java'))
    bindings={p.relative_to(ROOT).as_posix():digest(p) for p in inputs}
    environment=os.environ.copy();environment.update(ZIG_GLOBAL_CACHE_DIR=str(ROOT/'build/zig-cache'),
        ZIG_LOCAL_CACHE_DIR=str(ROOT/'build/zig-local'),TEMP=str(ROOT/'build/temp'),TMP=str(ROOT/'build/temp'))
    flags=(subprocess.BELOW_NORMAL_PRIORITY_CLASS|subprocess.CREATE_NO_WINDOW) if os.name=='nt' else 0
    common=[str(zig),'c++','-std=c++20','-O1','-g0','-I'+str(ROOT/'engine'),'-I'+str(ROOT/'platform')]
    sources=[ROOT/p for p in ('platform/resource_fd.cpp','platform/file_io.cpp','engine/resource_archive.cpp','tests/resource_descriptors.cpp')]
    program=directory/'resource-descriptors-tests.exe';result=directory/'resource-descriptors-tests.json'
    arm=directory/'resource_fd-aarch64.o'
    with (ROOT/'logs/android-storage-build.log').open('wb') as console:
        objects=[]
        for i,source in enumerate(sources):
            obj=directory/(str(i)+'-'+source.stem+'.obj');objects.append(obj)
            subprocess.run([*common,'-target','x86_64-windows-gnu','-c',str(source),'-o',str(obj)],
                cwd=ROOT,env=environment,creationflags=flags,stdout=console,stderr=subprocess.STDOUT,timeout=120,check=True)
        commands=[[*common,'-target','x86_64-windows-gnu',*map(str,objects),'-o',str(program)],
            [*common,'-target','aarch64-linux-musl','-c',str(ROOT/'platform/resource_fd.cpp'),'-o',str(arm)]]
        for source in ('android_resources','frame_metrics_jni'):
            commands.append([*common,'-target','x86_64-windows-gnu','-D__ANDROID__','-DSDL_STATIC',
                '-I'+str(ROOT/'tools/SDL-release-2.32.10/include'),'-I'+str(jdk/'include'),'-I'+str(jdk/'include/win32'),
                '-c',str(ROOT/'android/native'/f'{source}.cpp'),'-o',str(directory/f'{source}-jni-syntax.obj')])
        for command in commands:
            subprocess.run(command,cwd=ROOT,env=environment,creationflags=flags,stdout=console,stderr=subprocess.STDOUT,timeout=120,check=True)
        subprocess.run([str(program),str(ROOT/'res'),str(directory),str(result)],cwd=ROOT,creationflags=flags,
            stdout=console,stderr=subprocess.STDOUT,timeout=20,check=True)
        classes=directory/'java-classes';classes.mkdir(exist_ok=True)
        subprocess.run([str(jdk/'bin/javac.exe'),'-J-XX:ActiveProcessorCount=1','-J-Xmx128m','-encoding','UTF-8',
            '-d',str(classes),str(ROOT/'android/app/src/main/java/org/wa2/port/ArchiveNames.java'),
            str(ROOT/'android/app/src/main/java/org/wa2/port/ResourceLocation.java'),
            str(ROOT/'tests/android/org/wa2/port/ArchiveNamesTest.java'),
            str(ROOT/'tests/android/org/wa2/port/ResourceLocationTest.java')],cwd=ROOT,creationflags=flags,
            stdout=console,stderr=subprocess.STDOUT,timeout=30,check=True)
        names_result=directory/'archive-names-tests.json'
        subprocess.run([str(jdk/'bin/java.exe'),'-XX:ActiveProcessorCount=1','-Xmx128m','-cp',str(classes),
            'org.wa2.port.ArchiveNamesTest',str(names_result)],cwd=ROOT,creationflags=flags,
            stdout=console,stderr=subprocess.STDOUT,timeout=15,check=True)
        location_result=directory/'resource-location-tests.json'
        subprocess.run([str(jdk/'bin/java.exe'),'-XX:ActiveProcessorCount=1','-Xmx128m','-cp',str(classes),
            'org.wa2.port.ResourceLocationTest',str(location_result)],cwd=ROOT,creationflags=flags,
            stdout=console,stderr=subprocess.STDOUT,timeout=15,check=True)
    for name,value in bindings.items():
        if digest(ROOT/name)!=value:raise ValueError('Storage verification source changed: '+name)
    proof=json.loads(result.read_text());proof.update(source_sha256=bindings,
        filename_verification=json.loads(names_result.read_text()),host_test_sha256=digest(program),
        resource_location_verification=json.loads(location_result.read_text()),
        arm64_target='aarch64-linux-musl',arm64_object_sha256=digest(arm),
        native_jni_host_syntax_compiled=True,android_ndk_link_verified=False,
        android_directory_picker_device_verified=False,game_host_storage_connected=False,
        apk_created=False,published_runtime_changed=False)
    (ROOT/'logs/android-storage-verification.json').write_text(json.dumps(proof,indent=2)+'\n')
    print('PASS external PAK access:',proof['checks'],'descriptor checks and',proof['filename_verification']['cases'],
        'filename checks; no whole-archive extraction, game, or screen access.')


if __name__=='__main__':main()
