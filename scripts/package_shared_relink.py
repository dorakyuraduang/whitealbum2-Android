"""Collect the actual shared-host link inputs, not the retired scene host."""
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import zipfile

ROOT=Path(__file__).resolve().parents[1]


def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def collect(build,target,output_name,kind,directory):
    ninja=(build/'build.ninja').read_text()
    match=re.search(r'^build '+re.escape(target)+r': [^\n]+\n(?:  [^\n]*\n)*',ninja,re.M)
    if not match:raise ValueError('Actual host link rule is absent: '+target)
    rule=match[0];objects=rule.splitlines()[0].split(': ',1)[1].split(' |',1)[0].split()[1:]
    line=re.search(r'^  LINK_LIBRARIES = (.*)$',rule,re.M)
    if not line:raise ValueError('Actual host library list is absent')
    archives=list(dict.fromkeys(x for x in line[1].split() if x.endswith('.a')))
    materials={};recipe=dict(objects=[],archives=[],system_libraries=[])
    for category,inputs in (('objects',objects),('archives',archives)):
        for value in inputs:
            path=Path(value.replace('$:',':'));path=path if path.is_absolute() else build/path
            name=category+'/'+path.name
            if name in materials and digest(materials[name])!=digest(path):raise ValueError('Relink basename collision')
            materials[name]=path;recipe[category].append(name)
    if kind=='android':materials['native/libSDL2.so']=ROOT/'build/android-native-game/sdl2/libSDL2.so'
    else:
        recipe['system_libraries']=['comctl32','comdlg32','setupapi','winmm','imm32','version','ole32','oleaut32','uuid',
            'advapi32','user32','gdi32','shell32','kernel32','ws2_32','bcrypt','ntdll']
    recipe.update(target=kind,output_name=output_name)
    for name,path in materials.items():
        if not path.is_file():raise ValueError('Missing actual link input: '+str(path))
        destination=directory/kind/name;destination.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(path,destination)
    (directory/kind/'link.json').write_text(json.dumps(recipe,indent=2)+'\n')
    return {kind+'/'+name:path for name,path in materials.items()}


def package(artifacts,console,environment,flags):
    identity=hashlib.sha256(json.dumps(artifacts,sort_keys=True).encode()).hexdigest()[:16]
    directory=ROOT/'build/shared-relink'/identity;directory.mkdir(parents=True,exist_ok=True)
    materials={}
    materials.update(collect(ROOT/'build/shared-aot-android','actual_host/libwa2_android.so','libwa2_android.so','android',directory))
    materials.update(collect(ROOT/'build/shared-host-win64','WA2_SDL2_x64.exe','WA2_SDL2_x64.exe','windows',directory))
    for host,codec in (('android','arm64'),('windows','windows')):
        for name in ('config.h','config_components.h','config.asm','ffbuild/config.mak'):
            path=ROOT/'build/media-codec'/codec/name
            if path.is_file():materials[host+'/ffmpeg-config/'+name]=path
        materials[host+'/ffmpeg-build.json']=ROOT/('logs/media-codec-'+codec+'-build.json')
    for name in ('COPYING.LGPLv2.1','NOTICE.txt'):materials['ffmpeg/'+name]=ROOT/'third_party/ffmpeg'/name
    materials['ffmpeg/ffmpeg-9.0.2.tar.xz']=ROOT/'tools/downloads/ffmpeg-9.0.2.tar.xz'
    materials['build_media_codec.py']=ROOT/'scripts/build_media_codec.py'
    materials['shared_relink.py']=ROOT/'scripts/shared_relink.py'
    materials['SDL2-LICENSE.txt']=ROOT/'tools/SDL-release-2.32.10/LICENSE.txt'
    materials['MojoShader-LICENSE.txt']=ROOT/'tools/mojoshader-main/LICENSE.txt'
    materials['MojoShader-assembler-LICENSE.txt']=ROOT/'tools/mojoshader_assembler-main/LICENSE.txt'
    for name,path in materials.items():
        destination=directory/name;destination.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(path,destination)
    relinks={}
    for host,output,tool in (('android','libwa2_android.so',['--ndk',ROOT/'tools/android-sdk/ndk/27.0.12077973']),
                             ('windows','WA2_SDL2_x64.exe',['--zig',ROOT/'tools/python/ziglang/zig.exe'])):
        path=directory/(host+'-relinked-'+output)
        subprocess.run(list(map(str,[sys.executable,directory/'shared_relink.py','--target',host,*tool,'--output',path])),
            cwd=ROOT,env=environment,creationflags=flags,stdout=console,stderr=subprocess.STDOUT,check=True,timeout=180)
        relinks[host]=dict(sha256=digest(path),bytes=path.stat().st_size)
    records={name:dict(bytes=path.stat().st_size,sha256=digest(path)) for name,path in materials.items()}
    for host in ('android','windows'):
        name=host+'/link.json';records[name]=dict(bytes=(directory/name).stat().st_size,sha256=digest(directory/name))
    manifest=dict(schema='wa2.shared-relink.v1',artifacts=artifacts,materials=records,relink_recipe_verified=True,
        relinked_artifacts=relinks,game_resources_included=False,ndk='27.0.12077973',maximum_build_jobs=1)
    readme=('These materials match the APK and Windows executable identified in manifest.json.\n'
        'FFmpeg remains replaceable: rebuild compatible libraries with the supplied source and exact configurations, '
        'then replace the corresponding libav*.a files under android/archives or windows/archives.\n'
        'Android: python shared_relink.py --target android --ndk /path/to/ndk-r27 --output libwa2_android.so\n'
        'Windows: python shared_relink.py --target windows --zig /path/to/zig.exe --output WA2_SDL2_x64.exe\n'
        'For Android, replace lib/arm64-v8a/libwa2_android.so in an unsigned APK and align/sign it with your own SDK tools.\n'
        'No signing key or game PAK resources are included. This archive is not copied to the phone.\n')
    output=ROOT/'out/WA2-shared-relink.zip'
    with zipfile.ZipFile(output,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as archive:
        for name in records:archive.write(directory/name,name,compress_type=zipfile.ZIP_STORED if name.endswith('.tar.xz') else zipfile.ZIP_DEFLATED)
        archive.writestr('manifest.json',json.dumps(manifest,indent=2)+'\n');archive.writestr('README.txt',readme)
    return output
