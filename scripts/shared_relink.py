"""Relink the shared application with compatible replacement LGPL libraries.

Run from an extracted WA2-shared-relink.zip with Python 3 and Android NDK r27
or the Windows Zig toolchain specified in manifest.json. No game is launched.
"""
import argparse
import ctypes
import json
import os
from pathlib import Path
import subprocess


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target',choices=('android','windows'),required=True)
    parser.add_argument('--ndk',type=Path)
    parser.add_argument('--zig',type=Path)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--materials',type=Path,default=Path(__file__).resolve().parent)
    args=parser.parse_args();base=args.materials/args.target
    recipe=json.loads((base/'link.json').read_text())
    if os.name=='nt':
        kernel=ctypes.WinDLL('kernel32');kernel.GetCurrentProcess.restype=ctypes.c_void_p
        kernel.SetPriorityClass.argtypes=[ctypes.c_void_p,ctypes.c_uint]
        if not kernel.SetPriorityClass(kernel.GetCurrentProcess(),0x4000):raise ctypes.WinError()
    if args.target=='android':
        if not args.ndk:parser.error('--ndk is required for Android')
        host='windows-x86_64' if os.name=='nt' else 'darwin-x86_64' if os.uname().sysname=='Darwin' else 'linux-x86_64'
        toolchain=args.ndk/'toolchains/llvm/prebuilt'/host
        compiler=toolchain/'bin'/('clang++.exe' if os.name=='nt' else 'clang++')
        command=[str(compiler),'--target=aarch64-none-linux-android26','--sysroot='+str(toolchain/'sysroot'),
            '-shared','-fPIC','-Wl,--gc-sections','-Wl,--no-undefined','-Wl,-z,max-page-size=16384',
            '-Wl,-soname,libwa2_android.so','-Wl,--exclude-libs,ALL','-Wl,-Bsymbolic']
        tail=[str(base/'native/libSDL2.so'),'-llog','-landroid','-lm','-latomic','-nostdlib++','-lc++_shared']
    else:
        if not args.zig:parser.error('--zig is required for Windows')
        command=[str(args.zig),'c++','-target','x86_64-windows-gnu','-mwindows','-Wl,--gc-sections']
        tail=['-l'+name for name in recipe['system_libraries']]
    inputs=[base/name for name in recipe['objects']+recipe['archives']]
    if not inputs or not all(path.is_file() for path in inputs):raise ValueError('Incomplete application relink materials')
    command+=['-o',str(args.output),*[str(base/name) for name in recipe['objects']],'-Wl,--start-group',
        *[str(base/name) for name in recipe['archives']],'-Wl,--end-group',*tail]
    kwargs=dict(check=True)
    if os.name=='nt':kwargs['creationflags']=subprocess.BELOW_NORMAL_PRIORITY_CLASS|subprocess.CREATE_NO_WINDOW
    subprocess.run(command,**kwargs)


if __name__=='__main__':main()
