"""Check every Final Full Patch movie and original WMV3/WMA2 inputs in memory.

Provide the already extracted video PAKs, including their IC directory. Only
mvNNN.pak files are opened. No executable, game, screen or device is accessed.
"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import uuid

ROOT=Path(__file__).resolve().parents[1]


def digest(path):
    path=Path(path)
    if path.name.lower()=='wa2_chs.exe':raise ValueError('Source executable access is forbidden')
    with path.open('rb') as source:return hashlib.file_digest(source,'sha256').hexdigest()


def inspect(path):
    """Bounded ASF stream-header inspection, without decoding its payload."""
    result=dict(file=str(path.resolve()),bytes=path.stat().st_size)
    with path.open('rb') as source:
        head=source.read(30)
        if len(head)!=30 or str(uuid.UUID(bytes_le=head[:16]))!='75b22630-668e-11cf-a6d9-00aa0062ce6c':raise ValueError('Expected an ASF movie: '+str(path))
        length,count=struct.unpack_from('<QI',head,16)
        if not 30<=length<=1024*1024 or length>result['bytes'] or count>1024:raise ValueError('Invalid ASF header bounds')
        at=30
        for _ in range(count):
            source.seek(at);obj=source.read(24)
            if len(obj)!=24:raise ValueError('Truncated ASF object')
            guid=str(uuid.UUID(bytes_le=obj[:16]));size=struct.unpack_from('<Q',obj,16)[0]
            if not 24<=size<=length-at:raise ValueError('ASF object outside header')
            data=source.read(size-24)
            if guid=='b7dc0791-a9b7-11cf-8ee6-00c00c205365':
                if len(data)<54:raise ValueError('Truncated ASF stream')
                kind=str(uuid.UUID(bytes_le=data[:16]));type_bytes,error_bytes,flags=struct.unpack_from('<IIH',data,40)
                if flags&0x8000 or type_bytes>len(data)-54 or error_bytes>len(data)-54-type_bytes:raise ValueError('Encrypted or truncated movie stream')
                values=data[54:54+type_bytes]
                if kind=='f8699e40-5b4d-11cf-a8fd-00805f5c442b':
                    if len(values)<18:raise ValueError('Truncated ASF audio format')
                    tag,channels,rate=struct.unpack_from('<HHI',values)
                    result.update(audio_tag=tag,audio_channels=channels,audio_rate=rate)
                if kind=='bc19efc0-5b4d-11cf-a8fd-00805f5c442b':
                    if len(values)<31:raise ValueError('Truncated ASF video format')
                    width,height=struct.unpack_from('<II',values)
                    result.update(video_fourcc=values[27:31].decode('ascii'),width=width,height=height)
            at+=size
        if at!=length or 'video_fourcc' not in result or 'audio_tag' not in result:raise ValueError('Missing ASF movie streams')
        source.seek(0);result['header_sha256']=hashlib.sha256(source.read(length)).hexdigest()
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--patch-dir',type=Path,required=True)
    parser.add_argument('--original-dir',type=Path,required=True)
    parser.add_argument('--zig-cache',type=Path,required=True)
    parser.add_argument('--windows-sdl',type=Path,required=True)
    args=parser.parse_args()
    if os.name!='nt':raise SystemExit('This verifier uses the pinned local Windows toolchain')
    k=ctypes.WinDLL('kernel32');k.GetCurrentProcess.restype=ctypes.c_void_p
    k.SetPriorityClass.argtypes=[ctypes.c_void_p,ctypes.c_uint];k.SetProcessAffinityMask.argtypes=[ctypes.c_void_p,ctypes.c_size_t]
    k.GetProcessAffinityMask.argtypes=[ctypes.c_void_p,ctypes.POINTER(ctypes.c_size_t),ctypes.POINTER(ctypes.c_size_t)]
    process=k.GetCurrentProcess();current=ctypes.c_size_t();system=ctypes.c_size_t()
    if not k.SetPriorityClass(process,0x4000) or not k.GetProcessAffinityMask(process,ctypes.byref(current),ctypes.byref(system)) or not k.SetProcessAffinityMask(process,current.value&-current.value):raise ctypes.WinError()
    patch=args.patch_dir.resolve();original=args.original_dir.resolve()
    files=sorted(path for path in patch.rglob('*') if path.is_file() and re.fullmatch(r'mv\d{3}\.pak',path.name,re.I))
    if len(files)!=16:raise ValueError('Expected all 16 Final Full Patch video PAKs, including IC')
    fixtures=[('patch',path,inspect(path)) for path in files]
    fixtures += [('original',original/name,inspect(original/name)) for name in ('mv000.pak','mv100.pak')]
    for kind,path,metadata in fixtures:
        expected=('WMV2',0x162) if kind=='patch' else ('WMV3',0x161)
        if (metadata['video_fourcc'],metadata['audio_tag'])!=expected:raise ValueError('Unexpected movie codec pair: '+str(path))
    sources=[Path(__file__),ROOT/'config/media_codecs.json',ROOT/'scripts/build_media_codec.py',ROOT/'tests/movie_codecs.cpp',ROOT/'engine/movie_reader.cpp',ROOT/'engine/movie_reader.hpp',ROOT/'engine/movie.cmake',ROOT/'platform/file_io.cpp',ROOT/'platform/file_io.hpp',ROOT/'scripts/zig-toolchain.cmake']
    sources += [ROOT/'tools/media-codec/windows/lib'/('lib'+name+'.a') for name in ('avformat','avcodec','swscale','swresample','avutil')]
    sources += [path for path in (ROOT/'tools/media-codec/windows/include').rglob('*.h')]
    bindings={path.relative_to(ROOT).as_posix():digest(path) for path in sources}
    directory=ROOT/'build/shared-aot-refresh-x64';results=ROOT/'build/movie-codec-checks';results.mkdir(parents=True,exist_ok=True)
    env=os.environ.copy();env.update(CMAKE_BUILD_PARALLEL_LEVEL='1',ZIG_GLOBAL_CACHE_DIR=str(args.zig_cache.resolve()),ZIG_LOCAL_CACHE_DIR=str(directory/'zig-local'),TEMP=str(ROOT/'build/temp'),TMP=str(ROOT/'build/temp'))
    flags=subprocess.CREATE_NO_WINDOW|subprocess.BELOW_NORMAL_PRIORITY_CLASS
    cmake=ROOT/'tools/python/cmake/data/bin/cmake.exe';ninja=ROOT/'tools/python/bin/ninja.exe'
    reports=[]
    with (ROOT/'logs/movie-codec-verification.log').open('wb') as log:
        def run(command,timeout=300):
            log.write(('RUN '+subprocess.list2cmdline(list(map(str,command)))+'\n').encode());log.flush()
            subprocess.run(list(map(str,command)),cwd=ROOT,env=env,creationflags=flags,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=timeout)
        run([cmake,'-S',ROOT/'tests/shared-host-refresh','-B',directory,'-G','Ninja','-DCMAKE_MAKE_PROGRAM='+str(ninja),'-DCMAKE_TOOLCHAIN_FILE='+str(ROOT/'scripts/zig-toolchain.cmake'),'-DCMAKE_BUILD_TYPE=MinSizeRel','-DCMAKE_EXPORT_COMPILE_COMMANDS=ON','-DWA2_SHARED_SDL_ARCHIVE='+str(args.windows_sdl.resolve())])
        run([cmake,'--build',directory,'--parallel','1','--target','wa2_refresh_movie_codecs'])
        for kind,path,metadata in fixtures:
            label=path.relative_to(patch if kind=='patch' else original).as_posix().replace('/','_')
            report_file=results/(kind+'-'+label+'.json');full=path.name.lower() in ('mv000.pak','mv100.pak')
            print('Checking '+kind+' '+label+' (audio to EOF; '+('full video' if full else 'video prefixes')+'); memory only.',flush=True)
            run([directory/'wa2_refresh_movie_codecs.exe',path,report_file,'full' if full else 'prefix'])
            report=json.loads(report_file.read_text())
            if not report['passed'] or report['guards']!=7 or report['exact_pcm_yuv_prefixes']!=16 or not report['audio_to_eof'] or not report['zero_cross_track_backlog'] or not report['unsupported_codecs_explicit'] or report['video_to_eof']!=full:raise ValueError('Incomplete codec regression: '+label)
            if inspect(path)!=metadata:raise ValueError('Movie header changed during verification: '+label)
            reports.append(dict(kind=kind,name=label,metadata=metadata,checks=report))
    for name,sha in bindings.items():
        if digest(ROOT/name)!=sha:raise ValueError('Movie verification input changed: '+name)
    proof=dict(schema='wa2.movie-codec-verification.v1',source_sha256=bindings,test_program_sha256=digest(directory/'wa2_refresh_movie_codecs.exe'),patch_movies=16,baseline_movies=2,patch_codec_pair=['WMV2','WMAPRO'],original_codec_pair=['WMV3','WMAV2'],full_audio_files=18,full_video_files=sum(entry['checks']['video_to_eof'] for entry in reports),files=reports,same_shared_reader=True,original_resources_external=True,unknown_codecs_rejected=True,game_launched=False,screen_access=False,devices_opened=False,complete_gameplay_verified=False,passed=True)
    (ROOT/'logs/movie-codec-verification.json').write_text(json.dumps(proof,indent=2)+'\n',encoding='utf-8')
    print('PASS 16 patch movies and 2 original movies; bounded independent tracks, full audio and representative video EOF.',flush=True)


if __name__=='__main__':main()
