"""Provide matching native-engine link objects and LGPL source/configuration.

Only native production link rules are collected. The retained AOT archive may
supply missing *codec configuration files* after disposable builds were lost;
no old application object or original game resource is copied from it.
"""
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import zipfile
from build_native_hosts import ROOT,digest


def collect(build,target,host,directory):
    ninja=(build/'build.ninja').read_text(encoding='utf-8')
    match=re.search(r'^build '+re.escape(target)+r': [^\n]+\n(?:  [^\n]*\n)*',ninja,re.M)
    if not match:raise ValueError('Missing actual native link rule: '+target)
    rule=match[0];objects=rule.splitlines()[0].split(': ',1)[1].split(' |',1)[0].split()[1:]
    line=re.search(r'^  LINK_LIBRARIES = (.*)$',rule,re.M)
    if not line:raise ValueError('Missing actual native library order')
    inputs={};recipe=dict(schema='wa2.native-link-inputs.v1',target=host,objects=[],libraries=[])
    def material(value,category):
        if any(word in value.lower() for word in ('recompiled','generated','winrecomp','shared_aot','/port/')):raise ValueError('Legacy native relink dependency: '+value)
        path=Path(value.replace('$:',':'));path=path if path.is_absolute() else build/path
        if path.suffix not in ('.a','.o','.obj','.res','.so') or not path.is_file():raise ValueError('Unknown native link material: '+str(path))
        name=category+'/'+path.name
        if name in inputs and digest(inputs[name])!=digest(path):raise ValueError('Native relink basename collision: '+name)
        inputs[name]=path;return name
    for value in objects:recipe['objects'].append(material(value,'objects'))
    for value in line[1].split():
        if value.startswith('-'):
            if value!='-pthread' and not re.fullmatch(r'-l[A-Za-z0-9_+.-]+',value):raise ValueError('Unknown native system link flag: '+value)
            recipe['libraries'].append(value)
        else:recipe['libraries'].append(material(value,'shared' if value.endswith('.so') else 'archives'))
    for name,path in inputs.items():
        output=directory/host/name;output.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(path,output)
    recipe_path=directory/host/'link.json';recipe_path.write_text(json.dumps(recipe,indent=2)+'\n',encoding='utf-8')
    return {host+'/'+name:path for name,path in inputs.items()}|{host+'/link.json':recipe_path}


def package(directory,applications,run,inspect):
    directory=directory/'relink';directory.mkdir(parents=True,exist_ok=True);materials={}
    for host,target in (('android','libwa2_native_android.so'),('windows','WA2_Native_x64.exe')):
        build=ROOT/('build/native-'+('android' if host=='android' else 'desktop')+'-definition')
        materials.update(collect(build,target,host,directory))
    legacy=ROOT/'out/WA2-shared-relink.zip';recovered=[]
    for host,codec in (('android','arm64'),('windows','windows')):
        report=ROOT/('logs/media-codec-'+codec+'-build.json');proof=json.loads(report.read_text(encoding='utf-8'))
        for name,value in proof['libraries'].items():
            if digest(ROOT/'tools/media-codec'/codec/name)!=value['sha256']:raise ValueError('Native codec build proof does not match active archives')
        materials[host+'/ffmpeg-build.json']=report
        for name in ('config.h','config_components.h','ffbuild/config.mak'):
            path=ROOT/'build/media-codec'/codec/name;member=host+'/ffmpeg-config/'+name
            if not path.is_file():
                # Recover only named general-codec configuration, checking the
                # archived manifest and matching codec build report first.
                with zipfile.ZipFile(legacy) as archive:
                    manifest=json.loads(archive.read('manifest.json'));records=manifest['materials']
                    old_proof=archive.read(host+'/ffmpeg-build.json')
                    if hashlib.sha256(old_proof).hexdigest()!=records[host+'/ffmpeg-build.json']['sha256'] or json.loads(old_proof)!=proof:raise ValueError('Retained codec configuration is for another codec build')
                    data=archive.read(member)
                    if hashlib.sha256(data).hexdigest()!=records[member]['sha256']:raise ValueError('Damaged retained codec configuration: '+member)
                path=directory/member;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data);recovered.append(member)
            if name=='config.h' and digest(path)!=proof['config_sha256']:raise ValueError('Native codec configuration changed')
            materials[member]=path
    materials['native_relink.py']=ROOT/'scripts/native_relink.py'
    materials['build_media_codec.py']=ROOT/'scripts/build_media_codec.py'
    materials['ffmpeg/ffmpeg-9.0.2.tar.xz']=ROOT/'tools/downloads/ffmpeg-9.0.2.tar.xz'
    for name in ('COPYING.LGPLv2.1','NOTICE.txt'):materials['ffmpeg/'+name]=ROOT/'third_party/ffmpeg'/name
    materials['SDL2-LICENSE.txt']=ROOT/'tools/SDL-release-2.32.10/LICENSE.txt'
    expected=json.loads((ROOT/'logs/media-codec-arm64-build.json').read_text(encoding='utf-8'))['source']['ffmpeg_archive_sha256']
    if digest(materials['ffmpeg/ffmpeg-9.0.2.tar.xz'])!=expected:raise ValueError('Corresponding LGPL source archive changed')
    records={}
    for name,path in materials.items():
        destination=directory/name;destination.parent.mkdir(parents=True,exist_ok=True)
        if path.resolve()!=destination.resolve():shutil.copy2(path,destination)
        records[name]=dict(bytes=destination.stat().st_size,sha256=digest(destination))
    relinks={};llvm=ROOT/'tools/android-sdk/ndk/27.0.12077973/toolchains/llvm/prebuilt/windows-x86_64/bin'
    for host,output,tool in (('android','libwa2_native_android.so',['--ndk',ROOT/'tools/android-sdk/ndk/27.0.12077973']),
            ('windows','WA2_Native_x64.exe',['--zig',ROOT/'tools/python/ziglang/zig.exe'])):
        destination=directory/(host+'-relinked-'+output)
        run([sys.executable,directory/'native_relink.py','--target',host,*tool,'--output',destination])
        if host=='android':
            header=inspect([llvm/'llvm-readelf.exe','-h','-d',destination])
            if 'AArch64' not in header or '[libwa2_native_android.so]' not in header:raise ValueError('Relinked native Android identity changed')
        else:
            header=inspect([llvm/'llvm-readobj.exe','--file-headers',destination])
            if 'IMAGE_FILE_MACHINE_AMD64' not in header or 'IMAGE_SUBSYSTEM_WINDOWS_GUI' not in header:raise ValueError('Relinked native desktop identity changed')
        relinks[host]=dict(sha256=digest(destination),bytes=destination.stat().st_size)
    for name,value in records.items():
        if digest(directory/name)!=value['sha256']:raise ValueError('Native relink input changed: '+name)
    manifest=dict(schema='wa2.native-development-relink.v1',applications=applications,materials=records,
        relink_recipe_verified=True,relinked_artifacts=relinks,codec_configurations_recovered=recovered,
        game_resources_included=False,recompiled_game_objects_included=False,maximum_build_jobs=1)
    readme=('Native-engine development application link materials. These are not a complete-game compatibility claim.\n'
        'Applications and exact input hashes are identified in manifest.json. Replace compatible libav*.a files under '
        'android/archives or windows/archives, then use the supplied native_relink.py.\n'
        'Android: python native_relink.py --target android --ndk /path/to/ndk-r27 --output libwa2_native_android.so\n'
        'Windows: python native_relink.py --target windows --zig /path/to/zig.exe --output WA2_Native_x64.exe\n'
        'The accompanying FFmpeg source/configuration enables no GPL/nonfree components. The original build script '
        'records local paths; its exact configure arguments and generated configuration are supplied for reproduction.\n'
        'To use a relinked Android library, replace its APK entry, then align/sign using your own SDK tools and identity.\n'
        'No signing key, original game EXE, PAK, font artwork, private analysis cache or AOT game object is included.\n')
    output=directory/'WA2-native-development-relink.zip'
    with zipfile.ZipFile(output,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as archive:
        for name in records:archive.write(directory/name,name,compress_type=zipfile.ZIP_STORED if name.endswith('.tar.xz') else zipfile.ZIP_DEFLATED)
        archive.writestr('manifest.json',json.dumps(manifest,indent=2)+'\n');archive.writestr('README.txt',readme)
    with zipfile.ZipFile(output) as archive:
        if archive.testzip() is not None:raise ValueError('Damaged native development relink archive')
        for name,value in records.items():
            if hashlib.sha256(archive.read(name)).hexdigest()!=value['sha256']:raise ValueError('Native relink archive byte mismatch: '+name)
    return output,manifest
