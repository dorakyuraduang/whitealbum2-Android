"""Verify common movie formats and graceful media-failure completion in memory."""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]


def digest(path):
    if Path(path).name.lower()=='wa2_chs.exe':raise ValueError('Source executable access is forbidden')
    with Path(path).open('rb') as source:return hashlib.file_digest(source,'sha256').hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--original-movie',type=Path,required=True)
    parser.add_argument('--zig-cache',type=Path,required=True)
    parser.add_argument('--windows-sdl',type=Path,required=True)
    parser.add_argument('--sample',type=Path,action='append',default=[])
    args=parser.parse_args()
    k=ctypes.WinDLL('kernel32');k.GetCurrentProcess.restype=ctypes.c_void_p
    k.SetPriorityClass.argtypes=[ctypes.c_void_p,ctypes.c_uint];k.SetProcessAffinityMask.argtypes=[ctypes.c_void_p,ctypes.c_size_t]
    k.GetProcessAffinityMask.argtypes=[ctypes.c_void_p,ctypes.POINTER(ctypes.c_size_t),ctypes.POINTER(ctypes.c_size_t)]
    process=k.GetCurrentProcess();current=ctypes.c_size_t();system=ctypes.c_size_t()
    if not k.SetPriorityClass(process,0x4000) or not k.GetProcessAffinityMask(process,ctypes.byref(current),ctypes.byref(system)) or not k.SetProcessAffinityMask(process,current.value&-current.value):raise ctypes.WinError()
    build=ROOT/'build/shared-aot-refresh-x64';samples=ROOT/'build/movie-format-samples';flags=subprocess.CREATE_NO_WINDOW|subprocess.BELOW_NORMAL_PRIORITY_CLASS
    env=os.environ.copy();env.update(CMAKE_BUILD_PARALLEL_LEVEL='1',ZIG_GLOBAL_CACHE_DIR=str(args.zig_cache.resolve()),ZIG_LOCAL_CACHE_DIR=str(build/'zig-local'),TEMP=str(ROOT/'build/temp'),TMP=str(ROOT/'build/temp'))
    inputs=[Path(__file__),ROOT/'scripts/make_movie_format_samples.py',ROOT/'tests/movie_formats.cpp',ROOT/'tests/portable_movie_bridge.cpp',ROOT/'engine/movie_reader.cpp',ROOT/'engine/movie_reader.hpp',ROOT/'engine/movie_player.cpp',ROOT/'engine/movie_player.hpp',ROOT/'engine/movie.cmake',ROOT/'platform/compat/media.cpp',ROOT/'platform/compat/media.hpp',ROOT/'config/media_codecs.json']
    inputs += [ROOT/'tools/media-codec/windows/lib'/('lib'+name+'.a') for name in ('avformat','avcodec','swscale','swresample','avutil')]
    bindings={path.relative_to(ROOT).as_posix():digest(path) for path in inputs}
    cmake=ROOT/'tools/python/cmake/data/bin/cmake.exe';ninja=ROOT/'tools/python/bin/ninja.exe'
    with (ROOT/'logs/movie-format-verification.log').open('wb') as log:
        def run(command,timeout=300):
            log.write(('RUN '+subprocess.list2cmdline(list(map(str,command)))+'\n').encode());log.flush()
            subprocess.run(list(map(str,command)),cwd=ROOT,env=env,creationflags=flags,stdout=log,stderr=subprocess.STDOUT,timeout=timeout,check=True)
        run([sys.executable,ROOT/'scripts/make_movie_format_samples.py','--out',samples])
        run([cmake,'-S',ROOT/'tests/shared-host-refresh','-B',build,'-G','Ninja','-DCMAKE_MAKE_PROGRAM='+str(ninja),'-DCMAKE_TOOLCHAIN_FILE='+str(ROOT/'scripts/zig-toolchain.cmake'),'-DCMAKE_BUILD_TYPE=MinSizeRel','-DCMAKE_EXPORT_COMPILE_COMMANDS=ON','-DWA2_SHARED_SDL_ARCHIVE='+str(args.windows_sdl.resolve())])
        run([cmake,'--build',build,'--parallel','1','--target','wa2_refresh_movie_formats','wa2_refresh_movie_bridge'])
        files=[samples/name for name in ('own-h264-aac.mp4','own-h264-aac.flv','own-h264-aac.ts','own-mjpeg-pcm.avi','own-mjpeg-surround.avi','own-mp4-renamed.pak')]+[path.resolve() for path in args.sample]
        print('Decoding common containers and testing corrupt-header/mid-stream recovery; no game/window/device.',flush=True)
        run([build/'wa2_refresh_movie_formats.exe',build/'movie-formats.json',args.original_movie.resolve(),*files])
        run([build/'wa2_refresh_movie_bridge.exe',build/'movie-bridge.json'])
    formats=json.loads((build/'movie-formats.json').read_text());bridge=json.loads((build/'movie-bridge.json').read_text())
    if not formats['passed'] or formats['recoveries']!=2 or formats['files']<6 or not all(formats[key] for key in ('full_decode_to_eof','content_detection_for_pak','jpeg_pixel_conversion','aac_and_packed_pcm','surround_to_stereo','failed_movie_stops_audio_and_clears_frames','non_media_exceptions_preserved')):raise ValueError('Incomplete common movie/failed-player checks')
    if not bridge['passed'] or bridge['memory_backend_source_combinations']!=4 or bridge['parse_decode_failure_cases']!=16 or not all(bridge[key] for key in ('failure_completion_exactly_once','restart_after_media_failure','failed_render_does_not_replay_stale_source','unsupported_service_still_rejected')):raise ValueError('Incomplete media-to-source completion checks')
    for name,sha in bindings.items():
        if digest(ROOT/name)!=sha:raise ValueError('Movie format input changed: '+name)
    proof=dict(schema='wa2.movie-format-verification.v1',source_sha256=bindings,formats=formats,source_completion=bridge,samples=[dict(file=str(path),bytes=path.stat().st_size,sha256=digest(path)) for path in files],bad_movie_completes_source_wait=True,unknown_guest_faults_preserved=True,same_shared_reader=True,game_launched=False,screen_access=False,devices_opened=False,complete_gameplay_verified=False,passed=True)
    (ROOT/'logs/movie-format-verification.json').write_text(json.dumps(proof,indent=2)+'\n')
    print('PASS common-format decoding, safe media failure, exactly-once source completion and later video recovery.',flush=True)


if __name__=='__main__':main()
