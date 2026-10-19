"""Fetch the authorized generic decoder and one local build tool, not a game port."""
import ctypes
import json
import os
from pathlib import Path
import subprocess
import tarfile
from prepare_android_toolchain import download,digest
from verify_ffmpeg_release import verify

ROOT=Path(__file__).resolve().parents[1]


def main():
    if os.name=='nt':ctypes.windll.kernel32.SetPriorityClass(ctypes.windll.kernel32.GetCurrentProcess(),0x4000)
    cache=ROOT/'tools/downloads';cache.mkdir(parents=True,exist_ok=True)
    base='https://ffmpeg.org/releases/ffmpeg-9.0.2.tar.xz';archive=cache/'ffmpeg-9.0.2.tar.xz'
    download(base,archive);download(base+'.asc',cache/'ffmpeg-9.0.2.tar.xz.asc')
    download('https://ffmpeg.org/ffmpeg-devel.asc',cache/'ffmpeg-devel.asc')
    fingerprint=verify(archive,(cache/'ffmpeg-devel.asc').read_bytes(),(cache/'ffmpeg-9.0.2.tar.xz.asc').read_bytes())
    destination=ROOT/'tools/ffmpeg-9.0.2';stamp=destination/'.wa2-source.json'
    if not stamp.is_file():
        with tarfile.open(archive) as source:source.extractall(ROOT/'tools',filter='data')
        if not (destination/'configure').is_file():raise ValueError('FFmpeg configure script is missing')
        stamp.write_text(json.dumps(dict(url=base,sha256=digest(archive),signer_fingerprint=fingerprint),indent=2)+'\n')
    url='https://mirror.msys2.org/msys/x86_64/make-4.4.1-3-x86_64.pkg.tar.zst'
    checksum='af0bdba17f06fe037f0194069adaa31a8fe45f1a11381501896aea1fae37bd5d'
    package=cache/'make-4.4.1-3-x86_64.pkg.tar.zst';download(url,package,checksum=checksum)
    make=ROOT/'tools/media-build/bin/make.exe';make.parent.mkdir(parents=True,exist_ok=True)
    if not make.is_file() or make.stat().st_size<65536:
        temporary=make.with_suffix('.part')
        with temporary.open('wb') as target:subprocess.run(['C:/Windows/system32/tar.exe','-xOf',str(package),'usr/bin/make.exe'],
            cwd=ROOT,stdout=target,check=True,timeout=20,creationflags=subprocess.BELOW_NORMAL_PRIORITY_CLASS|subprocess.CREATE_NO_WINDOW)
        if temporary.stat().st_size<65536 or temporary.read_bytes()[:2]!=b'MZ':raise ValueError('Invalid GNU make executable')
        temporary.replace(make)
    utilities_url='https://mirror.msys2.org/msys/x86_64/coreutils-8.32-5-x86_64.pkg.tar.zst'
    utilities_sha='62dfee1c39fd15f99c39802b35e82643bc14fffc16d6c76d4001caa385ec77e3'
    utilities=cache/'coreutils-8.32-5-x86_64.pkg.tar.zst';download(utilities_url,utilities,checksum=utilities_sha)
    for name in ('chmod','install','ln'):
        target=make.parent/(name+'.exe')
        if target.is_file():continue
        with target.open('wb') as output:subprocess.run(['C:/Windows/system32/tar.exe','-xOf',str(utilities),'usr/bin/'+name+'.exe'],
            cwd=ROOT,stdout=output,check=True,timeout=20,creationflags=subprocess.BELOW_NORMAL_PRIORITY_CLASS|subprocess.CREATE_NO_WINDOW)
        if target.stat().st_size<32768 or target.read_bytes()[:2]!=b'MZ':raise ValueError('Invalid build utility')
    proof=dict(schema='wa2.media-sources.v1',ffmpeg_version='9.0.2',ffmpeg_official_url=base,ffmpeg_archive_sha256=digest(archive),
        detached_signature_verified=True,signer_fingerprint=fingerprint,make_official_url=url,make_archive_sha256=checksum,
        make_executable_sha256=digest(make),coreutils_official_url=utilities_url,coreutils_archive_sha256=utilities_sha,
        external_game_project_used=False,screen_access=False,apk_created=False)
    (ROOT/'logs/media-sources.json').write_text(json.dumps(proof,indent=2)+'\n')
    print('Official FFmpeg source signature verified; generic decoder source and GNU make prepared.',flush=True)


if __name__=='__main__':main()
