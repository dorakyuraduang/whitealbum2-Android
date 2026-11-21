"""Prepare a verified Main/SC APK release with matching LGPL relink materials.

Uses completed builds only. It relinks a finite component below normal priority,
never builds an APK, executes a game, copies PAKs or publishes a GitHub release.
"""
from __future__ import annotations
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import zipfile
from package_shared_relink import collect
from verify_special_contents import fresh,ROOT

def sha(path):
    value=hashlib.sha256()
    with Path(path).open('rb') as source:
        for data in iter(lambda:source.read(1024*1024),b''):value.update(data)
    return value.hexdigest()

def main():
    if os.name=='nt':
        kernel=ctypes.WinDLL('kernel32');kernel.GetCurrentProcess.restype=ctypes.c_void_p
        kernel.SetPriorityClass.argtypes=[ctypes.c_void_p,ctypes.c_uint]
        if not kernel.SetPriorityClass(kernel.GetCurrentProcess(),0x4000):raise ctypes.WinError()
    flags=getattr(subprocess,'CREATE_NO_WINDOW',0)|getattr(subprocess,'BELOW_NORMAL_PRIORITY_CLASS',0)
    env=os.environ.copy();env.update(CMAKE_BUILD_PARALLEL_LEVEL='1',ZIG_GLOBAL_CACHE_DIR=str(ROOT/'build/zig-cache'),ZIG_LOCAL_CACHE_DIR=str(ROOT/'build/zig-local'),TEMP=str(ROOT/'build/temp'),TMP=str(ROOT/'build/temp'))
    proof=fresh();package=json.loads((ROOT/'logs/integrated-launcher.json').read_text(encoding='utf-8'));apk=Path(package['file']);profile=json.loads((ROOT/'config/special_contents.json').read_text())
    if not package['passed'] or package['version']!=profile['android_version_name'] or sha(apk)!=package['sha256']:raise ValueError('Wrong or unverified integrated APK')
    special=proof['artifacts']['android']
    if special['sha256']!=package['preserved_entries']['lib/arm64-v8a/libwa2_special.so']:raise ValueError('APK SC library differs from checked source')
    with zipfile.ZipFile(apk) as archive:
        if archive.testzip() or any(name.lower().endswith(('.pak','.sav','.exe','.dat')) for name in archive.namelist()):raise ValueError('Invalid APK or unexpected game-resource payload')
        for name,expected in package['preserved_entries'].items():
            if hashlib.sha256(archive.read(name)).hexdigest()!=expected:raise ValueError('APK entry differs: '+name)
    main_zip=ROOT/'out/WA2-shared-relink.zip'
    with zipfile.ZipFile(main_zip) as archive:
        main_manifest=json.loads(archive.read('manifest.json'))
        if not main_manifest['relink_recipe_verified'] or main_manifest['artifacts']['android']['sha256']!=package['base_apk_sha256']:raise ValueError('Main relink materials do not match the retained base APK')
        for name,info in main_manifest['materials'].items():
            if hashlib.sha256(archive.read(name)).hexdigest()!=info['sha256']:raise ValueError('Main relink material differs: '+name)
        # Both modules use this exact FFmpeg build. Verify the retained Main
        # source/config provenance before combining the two material sets.
        for name in ('libavformat.a','libavcodec.a','libavutil.a','libswscale.a','libswresample.a'):
            if sha(ROOT/'tools/media-codec/arm64/lib'/name)!=main_manifest['materials']['android/archives/'+name]['sha256']:raise ValueError('Main/SC FFmpeg builds differ')
    version=package['version'];work=ROOT/'build/special-release'/version;work.mkdir(parents=True,exist_ok=True);directory=work/'special';directory.mkdir(exist_ok=True)
    materials=collect(ROOT/'build/shared-aot-android','actual_host/libwa2_special.so','libwa2_special.so','android',directory)
    materials['android/link.json']=directory/'android/link.json'
    for name in ('config.h','config_components.h','config.asm','ffbuild/config.mak'):
        source=ROOT/'build/media-codec/arm64'/name
        if source.is_file():materials['android/ffmpeg-config/'+name]=source
    materials['android/ffmpeg-build.json']=ROOT/'logs/media-codec-arm64-build.json'
    for name in ('COPYING.LGPLv2.1','NOTICE.txt'):materials['ffmpeg/'+name]=ROOT/'third_party/ffmpeg'/name
    materials['ffmpeg/ffmpeg-9.0.2.tar.xz']=ROOT/'tools/downloads/ffmpeg-9.0.2.tar.xz'
    expected=json.loads((ROOT/'logs/media-codec-arm64-build.json').read_text())['source']['ffmpeg_archive_sha256']
    if sha(materials['ffmpeg/ffmpeg-9.0.2.tar.xz'])!=expected:raise ValueError('Corresponding FFmpeg source changed')
    materials.update({'shared_relink.py':ROOT/'scripts/shared_relink.py','build_media_codec.py':ROOT/'scripts/build_media_codec.py','config/media_codecs.json':ROOT/'config/media_codecs.json','LICENSE':ROOT/'LICENSE','NOTICE.md':ROOT/'NOTICE.md','THIRD_PARTY_NOTICES.md':ROOT/'THIRD_PARTY_NOTICES.md','SDL2-LICENSE.txt':ROOT/'tools/SDL-release-2.32.10/LICENSE.txt','MojoShader-LICENSE.txt':ROOT/'tools/mojoshader-main/LICENSE.txt','MojoShader-assembler-LICENSE.txt':ROOT/'tools/mojoshader_assembler-main/LICENSE.txt','licenses/x87-math.txt':ROOT/'third_party/x87_math/NOTICE.txt','licenses/stb-vorbis.txt':ROOT/'third_party/stb/stb_vorbis.c'})
    for name in ('LICENSE','FONT-NOTICE.txt','README.md','source.json'):materials['licenses/wa2-subtitles/'+name]=ROOT/'third_party/wa2_subtitles'/name
    for name,path in materials.items():
        destination=directory/name;destination.parent.mkdir(parents=True,exist_ok=True)
        if path.resolve()!=destination.resolve():shutil.copy2(path,destination)
    ndk=ROOT/'tools/android-sdk/ndk/27.0.12077973';llvm=ndk/'toolchains/llvm/prebuilt/windows-x86_64/bin';relinked=work/'libwa2_special-relinked.so'
    with (ROOT/'logs/special-release-relink.log').open('wb') as log:
        subprocess.run([sys.executable,str(directory/'shared_relink.py'),'--target','android','--ndk',str(ndk),'--output',str(relinked)],env=env,creationflags=flags,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=180)
    def symbols(path):
        text=subprocess.run([str(llvm/'llvm-nm.exe'),'-D','--defined-only',str(path)],env=env,creationflags=flags,capture_output=True,text=True,check=True,timeout=30).stdout
        return {line.split()[-1] for line in text.splitlines() if line.strip()}
    if symbols(relinked)!=symbols(Path(special['file'])):raise ValueError('Relinked public exports differ')
    data=relinked.read_bytes();offset=struct.unpack_from('<Q',data,32)[0];size,count=struct.unpack_from('<HH',data,54)
    alignments=[struct.unpack_from('<Q',data,offset+n*size+48)[0] for n in range(count) if struct.unpack_from('<I',data,offset+n*size)[0]==1]
    if not alignments or any(n<16384 for n in alignments):raise ValueError('Relinked SC lacks 16 KiB alignment')
    dynamic=subprocess.run([str(llvm/'llvm-readelf.exe'),'-d',str(relinked)],env=env,creationflags=flags,capture_output=True,text=True,check=True,timeout=30).stdout
    if not re.search(r'\(SONAME\).*\[libwa2_special\.so\]',dynamic):raise ValueError('Relinked SC SONAME differs')
    records={name:dict(bytes=(directory/name).stat().st_size,sha256=sha(directory/name)) for name in materials}
    manifest=dict(schema='wa2.special-release.v1',version=version,version_code=package['version_code'],apk=dict(name=apk.name,sha256=package['sha256'],bytes=apk.stat().st_size),native_libraries={name:digest for name,digest in package['preserved_entries'].items() if name.startswith('lib/')},main_relink=dict(name='main/WA2-shared-relink.zip',sha256=sha(main_zip),base_apk_sha256=package['base_apk_sha256']),special_materials=records,special_relink_verified=True,relinked_special_sha256=sha(relinked),matching_exports=True,elf_load_alignments=alignments,game_resources_included=False,private_analysis_included=False,game_launched=False,screen_access=False,whole_game_compatibility_verified=False)
    output=ROOT/'out/releases'/version;output.mkdir(parents=True,exist_ok=True);bundle=output/('WA2-'+version+'-relink.zip')
    with zipfile.ZipFile(bundle,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as archive:
        archive.write(main_zip,'main/WA2-shared-relink.zip',compress_type=zipfile.ZIP_STORED)
        for name in materials:archive.write(directory/name,'special/'+name,compress_type=zipfile.ZIP_STORED if name.endswith('.tar.xz') else zipfile.ZIP_DEFLATED)
        archive.writestr('manifest.json',json.dumps(manifest,indent=2)+'\n')
        archive.writestr('README.txt','Matching LGPL relink materials for the integrated Main + SC APK named in manifest.json.\nMain: extract main/WA2-shared-relink.zip and follow its README. The retained Main library is unchanged from that verified base APK.\nSC: extract this archive, then run: python special/shared_relink.py --target android --ndk /path/to/android-ndk-r27 --output libwa2_special.so\nRebuild compatible FFmpeg libraries from the included source/configuration and replace special/android/archives/libav*.a and libsw*.a before relinking.\nReplace the matching native library in an unsigned APK, then align/sign with your own tools and signing identity.\nNo signing keys, PAK resources, external SC captions or private analysis caches are included. Nothing is installed or launched by these relink recipes.\n')
    tools=output/'WA2-SC-English-resource-tools.zip'
    with zipfile.ZipFile(tools,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as archive:
        for name in ('prepare_special_english_resources.py','prepare_special_english.py','special_mkv_subtitles.py'):archive.write(ROOT/'scripts'/name,'scripts/'+name)
        archive.write(ROOT/'LICENSE','LICENSE')
        archive.writestr('README.txt','SC English resource preparation / SC 英文资源准备\nPython 3.9+ standard library only. Supply your own full English SC patch v1.0 ZIP and original SC resources.\nRun from this extracted folder:\npython scripts/prepare_special_english_resources.py "path/to/White Album 2 Special Contents full patch (v1.0).zip" --base-root "path/to/original SC folder"\nCopy en.pak, subtext_en.dat, mv000_en.pak and credits_en.pak from out/special-english-resources/ into the SC folder selected in the Android launcher, then choose SC and English. Keep the original PAKs and filenames.\nNo private AOT cache or compiler is needed. Do not use --prepare-aot for resource preparation. No PAK, caption text or original executable is included here; supplied executables are checked as data and never executed.\n日文补充字幕未随此补丁提供。中文与英文字幕随所选版本自动读取，SC 没有外置字幕选择入口。\n')
    assets=[apk,bundle,tools];sums=output/'SHA256SUMS.txt';sums.write_text(''.join(sha(p)+'  '+p.name+'\n' for p in assets),encoding='utf-8')
    manifest['assets']=[dict(file=str(p),sha256=sha(p),bytes=p.stat().st_size) for p in [*assets,sums]]
    (ROOT/'logs/special-release-verification.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(dict(passed=True,version=version,assets=manifest['assets'],special_relink_verified=True,apk_rebuilt=False,game_launched=False)),flush=True)

if __name__=='__main__':main()
