"""Provide matching LGPL source/configuration and application link materials."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile

ROOT=Path(__file__).resolve().parents[1]


def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def package_relink(apk,console,environment,flags):
    build=ROOT/'build/android-native-game';directory=ROOT/'build/android-relink';directory.mkdir(parents=True,exist_ok=True)
    materials={}
    for path in build.rglob('libwa2_*.a'):materials['application/libs/'+path.name]=path
    for path in (build/'CMakeFiles/wa2_android.dir/native').glob('*.o'):materials['application/entry/'+path.name]=path
    for name in ('libavformat.a','libavcodec.a','libavutil.a'):materials['ffmpeg/lib/'+name]=ROOT/'tools/media-codec/arm64/lib'/name
    for name in ('config.h','config_components.h','config.asm'): 
        path=ROOT/'build/media-codec/arm64'/name
        if path.is_file():materials['ffmpeg/config/'+name]=path
    materials['ffmpeg/config/config.mak']=ROOT/'build/media-codec/arm64/ffbuild/config.mak'
    for name in ('COPYING.LGPLv2.1','NOTICE.txt'):materials['ffmpeg/'+name]=ROOT/'third_party/ffmpeg'/name
    materials['ffmpeg/ffmpeg-9.0.2.tar.xz']=ROOT/'tools/downloads/ffmpeg-9.0.2.tar.xz'
    materials['ffmpeg/build-configuration.json']=ROOT/'logs/media-codec-arm64-build.json'
    materials['native/libSDL2.so']=build/'size-check/libSDL2.so'
    materials['native/SDL2-LICENSE.txt']=ROOT/'tools/SDL-release-2.32.10/LICENSE.txt'
    materials['android_relink.py']=ROOT/'scripts/android_relink.py'
    materials['build_media_codec.py']=ROOT/'scripts/build_media_codec.py'
    # Verify the same relink recipe which is delivered, without launching code.
    for name,path in materials.items():
        if not path.is_file():raise ValueError('Missing relink material: '+str(path))
        destination=directory/name;destination.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(path,destination)
    output=directory/'libwa2_android.so'
    subprocess.run([sys.executable,directory/'android_relink.py','--ndk',ROOT/'tools/android-sdk/ndk/27.0.12077973','--output',output],
        cwd=directory,env=environment,creationflags=flags,stdout=console,stderr=subprocess.STDOUT,check=True,timeout=120)
    readelf=ROOT/'tools/android-sdk/ndk/27.0.12077973/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-readelf.exe'
    subprocess.run([str(readelf),'-h','-l',str(output)],creationflags=flags,stdout=console,stderr=subprocess.STDOUT,check=True,timeout=15)
    records={name:dict(bytes=path.stat().st_size,sha256=digest(path)) for name,path in materials.items()}
    manifest=dict(schema='wa2.android-relink.v1',apk_sha256=digest(apk),materials=records,ndk='27.0.12077973',api=26,abi='arm64-v8a',
        relink_recipe_verified=True,relinked_library_sha256=digest(output),game_resources_included=False)
    readme=('This archive accompanies the APK with SHA256 '+manifest['apk_sha256']+'.\n\n'
        'It contains the corresponding FFmpeg source, exact configuration, and native application archives/entry objects. '
        'It contains no game assets. The APK reads the original PAKs from Wa2Res.\n\n'
        'To use a compatible modified LGPL library, rebuild the minimal FFmpeg configuration for Android ARM64, '
        'replace ffmpeg/lib/*.a and run:\n'
        '  python android_relink.py --ndk /path/to/android-ndk-r27 --output libwa2_android.so\n\n'
        'Replace lib/arm64-v8a/libwa2_android.so in an unsigned copy of the APK, then align and sign it with your own '
        'Android SDK zipalign/apksigner tools. The local development signing key is not included or required.\n')
    destination=ROOT/'out/android/WA2-native-relink.zip'
    with zipfile.ZipFile(destination,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as archive:
        for name,path in materials.items():archive.write(path,name,compress_type=zipfile.ZIP_STORED if name.endswith('.tar.xz') else zipfile.ZIP_DEFLATED)
        archive.writestr('manifest.json',json.dumps(manifest,indent=2)+'\n');archive.writestr('README.txt',readme)
    manifest.update(archive=str(destination),archive_bytes=destination.stat().st_size,archive_sha256=digest(destination))
    (ROOT/'logs/android-relink-verification.json').write_text(json.dumps(manifest,indent=2)+'\n')
    return destination
