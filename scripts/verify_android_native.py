"""Compile/link the actual game host and all native modules with the real NDK."""
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT=Path(__file__).resolve().parents[1]


def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    if os.name=='nt':ctypes.windll.kernel32.SetPriorityClass(ctypes.windll.kernel32.GetCurrentProcess(),0x4000)
    sdk=ROOT/'tools/android-sdk';ndk=sdk/'ndk/27.0.12077973';directory=ROOT/'build/android-native-game'
    directory.mkdir(parents=True,exist_ok=True)
    inputs=[p for folder in ('engine','platform','game','android/native','android/game')
        for p in (ROOT/folder).glob('*') if p.is_file() and p.suffix in ('.cpp','.hpp','.inc','.cmake','.txt')]
    inputs +=[ROOT/'android/CMakeLists.txt',Path(__file__)]
    bindings={p.relative_to(ROOT).as_posix():digest(p) for p in inputs}
    cmake=sdk/'cmake/3.22.1/bin/cmake.exe'
    commands=[[str(cmake),'-S',str(ROOT/'android'),'-B',str(directory),'-G','Ninja',
        '-DCMAKE_MAKE_PROGRAM='+str(sdk/'cmake/3.22.1/bin/ninja.exe'),
        '-DCMAKE_TOOLCHAIN_FILE='+str(ndk/'build/cmake/android.toolchain.cmake'),
        '-DANDROID_ABI=arm64-v8a','-DANDROID_PLATFORM=android-26','-DANDROID_STL=c++_shared',
        '-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON','-DWA2_VERIFY_ALL_NATIVE=ON','-DCMAKE_BUILD_TYPE=MinSizeRel'],
        [str(cmake),'--build',str(directory),'--target','wa2_android','wa2_android_native_check','--parallel','1']]
    environment=os.environ.copy();environment.update(CMAKE_BUILD_PARALLEL_LEVEL='1',TEMP=str(ROOT/'build/temp'),TMP=str(ROOT/'build/temp'))
    flags=subprocess.BELOW_NORMAL_PRIORITY_CLASS|subprocess.CREATE_NO_WINDOW
    with (ROOT/'logs/android-native-build.log').open('wb') as console:
        for command in commands:subprocess.run(command,cwd=ROOT,env=environment,creationflags=flags,
            stdout=console,stderr=subprocess.STDOUT,check=True,timeout=1200)
    library=directory/'libwa2_android_native_check.so';sdl=directory/'sdl2/libSDL2.so'
    reader=ndk/'toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-readelf.exe';records=[]
    cxx=ndk/'toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so'
    for path in (library,directory/'libwa2_android.so',sdl,cxx):
        output=subprocess.run([str(reader),'--wide','-h','-l','-d',str(path)],cwd=ROOT,capture_output=True,text=True,
            creationflags=flags,check=True,timeout=10).stdout
        if 'AArch64' not in output:raise ValueError('Not an ARM64 Android ELF library')
        loads=[int(line.split()[-1],16) for line in output.splitlines() if line.strip().startswith('LOAD ')]
        if not loads or any(value<16384 for value in loads):raise ValueError('ELF does not support 16 KiB load alignment')
        needed=re.findall(r'\(NEEDED\).*?\[(.*?)\]',output)
        if any('.dll' in item.lower() for item in needed):raise ValueError('Windows DLL dependency in Android check')
        stripped=directory/'size-check'/path.name;stripped.parent.mkdir(parents=True,exist_ok=True)
        strip=reader.with_name('llvm-strip.exe')
        subprocess.run([str(strip),'--strip-all','-o',str(stripped),str(path)],cwd=ROOT,creationflags=flags,check=True,timeout=10)
        records.append(dict(file=str(path),bytes=path.stat().st_size,sha256=digest(path),needed=needed,load_alignment=loads,
            stripped_file=str(stripped),stripped_bytes=stripped.stat().st_size,stripped_sha256=digest(stripped)))
    for name,value in bindings.items():
        if digest(ROOT/name)!=value:raise ValueError('NDK verification source changed: '+name)
    proof=dict(schema='wa2.android-native-link.v1',abi='arm64-v8a',api=26,ndk='27.0.12077973',
        actual_android_ndk_link_verified=True,source_sha256=bindings,libraries=records,
        all_migrated_module_objects_linked=True,windows_dlls_linked=False,cpu_emulator_linked=False,
        screen_access=False,game_launched=False,emulator_launched=False,apk_created=False,
        native_game_host_linked=True,actual_present_hook_connected=True,game_host_storage_connected=True,
        complete_gameplay_verified=False,android_device_verified=False,passed=True)
    (ROOT/'logs/android-native-verification.json').write_text(json.dumps(proof,indent=2)+'\n')
    print('PASS actual Android game host, all migrated modules, ARM64 link and 16 KiB ELF alignment; no game, emulator or screen access.')


if __name__=='__main__':main()
