"""Build the authorized movie and legacy WAV decoders, one low-priority job."""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess

ROOT=Path(__file__).resolve().parents[1]
GIT=Path('C:/Users/Administrator/.cache/codex-runtimes/codex-primary-runtime/dependencies/native/git')
NDK=ROOT/'tools/android-sdk/ndk/27.0.12077973/toolchains/llvm/prebuilt/windows-x86_64'


def digest(path):
    result=hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda:source.read(1024*1024),b''):result.update(block)
    return result.hexdigest()


def posix(path):
    path=Path(path).resolve().as_posix()
    return '/'+path[0].lower()+path[2:] if len(path)>2 and path[1]==':' else path


def wrapper(path,command):
    path.write_text('#!/bin/sh\nexec '+shlex.join(command)+' "$@"\n',encoding='utf-8',newline='\n')


def main():
    arguments=argparse.ArgumentParser();arguments.add_argument('--target',choices=['windows','arm64'],required=True)
    args=arguments.parse_args()
    if os.name=='nt':
        kernel=ctypes.WinDLL('kernel32');kernel.GetCurrentProcess.restype=ctypes.c_void_p
        kernel.SetPriorityClass.argtypes=[ctypes.c_void_p,ctypes.c_uint]
        if not kernel.SetPriorityClass(kernel.GetCurrentProcess(),0x4000):raise ctypes.WinError(ctypes.get_last_error())
    source=ROOT/'tools/ffmpeg-9.0.2';source_proof=json.loads((ROOT/'logs/media-sources.json').read_text())
    if source_proof.get('detached_signature_verified') is not True:raise ValueError('Unverified codec source')
    work=ROOT/'build/media-codec'/args.target;work.mkdir(parents=True,exist_ok=True)
    prefix=ROOT/'tools/media-codec'/args.target;prefix.mkdir(parents=True,exist_ok=True)
    environment=os.environ.copy();environment.update(TEMP=str(ROOT/'build/temp'),TMP=str(ROOT/'build/temp'),
        ZIG_GLOBAL_CACHE_DIR=str(ROOT/'build/zig-cache'),ZIG_LOCAL_CACHE_DIR=str(ROOT/'build/zig-local'),
        MSYS2_PATH_TYPE='inherit',LC_ALL='C',MAKEFLAGS='-j1')
    environment['PATH']=str(ROOT/'tools/media-build/bin')+os.pathsep+str(GIT/'usr/bin')+os.pathsep+environment.get('PATH','')
    flags=subprocess.BELOW_NORMAL_PRIORITY_CLASS|subprocess.CREATE_NO_WINDOW
    shell=GIT/'usr/bin/sh.exe'
    zig=posix(ROOT/'tools/python/ziglang/zig.exe')
    wrapper(work/'hostcc',[zig,'cc','-target','x86_64-windows-gnu'])
    if args.target=='windows':
        wrapper(work/'cc',[zig,'cc','-target','x86_64-windows-gnu'])
        wrapper(work/'ar',[zig,'ar']);wrapper(work/'ranlib',[zig,'ranlib'])
        # Keep configure's GNU-ld-only --pic-executable flag out of the host
        # checks. Debug info is only in the Windows unit-test dependency.
        platform=['--target-os=mingw32','--arch=x86_64','--disable-asm','--enable-debug=1','--disable-pthreads','--enable-w32threads']
    else:
        if not (NDK/'bin/clang.exe').is_file():raise ValueError('Install the pinned official NDK first')
        wrapper(work/'cc',[posix(NDK/'bin/clang.exe'),'--target=aarch64-linux-android26'])
        wrapper(work/'ar',[posix(NDK/'bin/llvm-ar.exe')]);wrapper(work/'ranlib',[posix(NDK/'bin/llvm-ranlib.exe')])
        platform=['--target-os=android','--arch=aarch64','--cpu=armv8-a','--disable-debug','--enable-pthreads','--disable-w32threads']
    configuration=[posix(source/'configure'),'--prefix='+posix(prefix),'--enable-cross-compile',*platform,
        '--cc='+posix(work/'cc'),'--ar='+posix(work/'ar'),'--ranlib='+posix(work/'ranlib'),'--host-cc='+posix(work/'hostcc'),
        '--nm='+posix(NDK/'bin/llvm-nm.exe'),'--strip='+posix(NDK/'bin/llvm-strip.exe'),
        '--disable-everything','--disable-autodetect','--disable-network','--disable-programs','--disable-doc',
        '--disable-shared','--enable-static','--enable-pic','--enable-small',
        '--disable-avdevice','--disable-avfilter','--disable-swscale','--disable-swresample',
        '--disable-os2threads',
        '--enable-avformat','--enable-avcodec','--enable-avutil',
        '--enable-demuxer=asf','--enable-decoder=wmv3,wmav2,adpcm_ms,adpcm_ima_wav,pcm_alaw,pcm_mulaw',
        '--extra-cflags=-ffunction-sections -fdata-sections']
    log=ROOT/('logs/media-codec-'+args.target+'-build.log')
    config_key=hashlib.sha256(json.dumps(configuration).encode()).hexdigest()
    configured=work/'.wa2-configured.json'
    with log.open('ab') as console:
        if not configured.is_file() or json.loads(configured.read_text()).get('key')!=config_key:
            print('Configuring minimal codec:',args.target,flush=True)
            command='cd '+shlex.quote(posix(work))+' && '+shlex.join(configuration)
            subprocess.run([str(shell),'-c',command],cwd=ROOT,env=environment,creationflags=flags,
                stdout=console,stderr=subprocess.STDOUT,check=True,timeout=900)
            configured.write_text(json.dumps(dict(key=config_key,configuration=configuration),indent=2)+'\n')
        for target in ('all','install'):
            print('Codec',args.target,target,'(one low-priority job)',flush=True)
            command='cd '+shlex.quote(posix(work))+' && make -j1 '+target
            subprocess.run([str(shell),'-c',command],cwd=ROOT,env=environment,creationflags=flags,
                stdout=console,stderr=subprocess.STDOUT,check=True,timeout=1800)
    libraries={str(p.relative_to(prefix)):dict(bytes=p.stat().st_size,sha256=digest(p)) for p in sorted((prefix/'lib').glob('*.a'))}
    if set(Path(p).name for p in libraries)!={'libavformat.a','libavcodec.a','libavutil.a'}:raise ValueError('Unexpected codec libraries')
    config_text=(work/'config.h').read_text()
    if '#define HAVE_THREADS 1' not in config_text:raise ValueError('Codec initialization must be thread-safe')
    proof=dict(schema='wa2.media-codec-build.v1',target=args.target,source=source_proof,configuration=configuration,
        config_sha256=digest(work/'config.h'),libraries=libraries,license='LGPL-2.1-or-later',
        thread_safe_initialization=True,decoder_thread_count_policy=1,
        gpl_enabled=False,nonfree_enabled=False,external_game_project_used=False,game_launched=False,screen_access=False)
    (ROOT/('logs/media-codec-'+args.target+'-build.json')).write_text(json.dumps(proof,indent=2)+'\n')
    print('Minimal native codec built:',args.target,flush=True)


if __name__=='__main__':main()
