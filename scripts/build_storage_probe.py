"""Build a same-signature, Activity-free device I/O probe; never launch a game."""
import argparse,ctypes,hashlib,json,os,subprocess,zipfile
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--target-package',choices=('org.wa2.port','org.wa2.port.nativeengine'),default='org.wa2.port');parser.add_argument('--native-build',type=Path);args=parser.parse_args()
    k=ctypes.WinDLL('kernel32',use_last_error=True);k.GetCurrentProcess.restype=ctypes.c_void_p;k.SetPriorityClass.argtypes=[ctypes.c_void_p,ctypes.c_uint];k.SetProcessAffinityMask.argtypes=[ctypes.c_void_p,ctypes.c_size_t]
    if not k.SetPriorityClass(k.GetCurrentProcess(),0x4000) or not k.SetProcessAffinityMask(k.GetCurrentProcess(),1):raise ctypes.WinError()
    flags=subprocess.CREATE_NO_WINDOW|subprocess.BELOW_NORMAL_PRIORITY_CLASS
    directory=ROOT/'build/android-storage-probe';directory.mkdir(parents=True,exist_ok=True);classes=directory/'classes';classes.mkdir(exist_ok=True);dex=directory/'dex';dex.mkdir(exist_ok=True);(ROOT/'logs').mkdir(exist_ok=True)
    jdk=ROOT/'tools/source-analysis/jdk-21.0.12.1+1';tools=ROOT/'tools/android-sdk/build-tools/34.0.0';api=ROOT/'tools/android-api-35/android.jar';key=ROOT/'private/android-signing/wa2-native-dev.p12'
    if not key.is_file():raise ValueError('The retained application signing key is required; never regenerate it')
    manifest=directory/'AndroidManifest.xml';manifest.write_text('<?xml version="1.0" encoding="utf-8"?>\n<manifest xmlns:android="http://schemas.android.com/apk/res/android" package="org.wa2.port.storageprobe" android:versionCode="1" android:versionName="1"><uses-sdk android:minSdkVersion="26" android:targetSdkVersion="35"/><application android:label="WA2 Storage Probe" android:hasCode="true" android:debuggable="true"/><instrumentation android:name="org.wa2.probe.StorageProbe" android:targetPackage="'+args.target_package+'" android:functionalTest="true"/></manifest>\n')
    with (ROOT/'logs/storage-probe-build.log').open('wb') as log:
        def run(cmd):subprocess.run(list(map(str,cmd)),cwd=ROOT,creationflags=flags,stdout=log,stderr=subprocess.STDOUT,timeout=120,check=True)
        native_libraries=[]
        if args.native_build:
            build=args.native_build.resolve();cmake=ROOT/'tools/python/cmake/data/bin/cmake.exe'
            if not (build/'CMakeCache.txt').is_file():raise ValueError('First configure and verify the shared Android ARM64 build')
            # Reuse the already configured ARM64 build and unchanged static
            # objects; serialize with every other build/test in this workspace.
            run([cmake,'-S',ROOT/'tests/shared-aot-android','-B',build,'-DWA2_DEVICE_STORAGE_PROBE=ON'])
            subprocess.run(list(map(str,[cmake,'--build',build,'--parallel','1','--target','wa2_storage_probe'])),cwd=ROOT,creationflags=flags,stdout=log,stderr=subprocess.STDOUT,timeout=3600,check=True)
            native_libraries=[build/'libwa2_storage_probe.so',ROOT/'build/android-native-game/sdl2/libSDL2.so',ROOT/'tools/android-sdk/ndk/27.0.12077973/toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so']
        run([jdk/'bin/javac.exe','-J-XX:ActiveProcessorCount=1','-J-Xmx128m','-encoding','UTF-8','-source','8','-target','8','-cp',api,'-d',classes,ROOT/'tests/android-device/StorageProbe.java'])
        jar=directory/'classes.jar';run([jdk/'bin/jar.exe','--create','--file',jar,'-C',classes,'.'])
        run([jdk/'bin/java.exe','-XX:ActiveProcessorCount=1','-XX:+UseSerialGC','-Xmx128m','-cp',tools/'lib/d8.jar','com.android.tools.r8.D8','--min-api','26','--lib',api,'--output',dex,jar])
        unsigned=directory/'unsigned.apk';run([tools/'aapt2.exe','link','-I',api,'--manifest',manifest,'-o',unsigned])
        with zipfile.ZipFile(unsigned,'a') as package:
            package.write(dex/'classes.dex','classes.dex',compress_type=zipfile.ZIP_DEFLATED)
            for library in native_libraries:package.write(library,'lib/arm64-v8a/'+library.name,compress_type=zipfile.ZIP_DEFLATED)
        aligned=directory/'aligned.apk';run([tools/'zipalign.exe','-f','4',unsigned,aligned])
        signed=directory/'WA2-storage-probe.apk';run([jdk/'bin/java.exe','-XX:ActiveProcessorCount=1','-Xmx128m','-jar',tools/'lib/apksigner.jar','sign','--ks',key,'--ks-key-alias','wa2-dev','--ks-pass','pass:android','--key-pass','pass:android','--out',signed,aligned])
        run([jdk/'bin/java.exe','-XX:ActiveProcessorCount=1','-Xmx128m','-jar',tools/'lib/apksigner.jar','verify',signed]);run([tools/'zipalign.exe','-c','4',signed])
    proof=dict(file=str(signed),sha256=hashlib.sha256(signed.read_bytes()).hexdigest(),target_package=args.target_package,game_launched=False,screen_access=False,passed=True)
    (ROOT/'logs/storage-probe-build.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps(proof),flush=True)
if __name__=='__main__':main()
