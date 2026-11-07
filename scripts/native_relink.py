"""Relink native-engine application objects with compatible LGPL replacements.

Run from the extracted native-development relink archive. This script only
invokes a linker; it never installs, loads or starts the resulting application.
"""
import argparse
import ctypes
import json
import os
from pathlib import Path
import re
import subprocess


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target',choices=('android','windows'),required=True)
    parser.add_argument('--materials',type=Path,default=Path(__file__).resolve().parent)
    parser.add_argument('--ndk',type=Path)
    parser.add_argument('--zig',type=Path)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();base=(args.materials/args.target).resolve()
    recipe=json.loads((base/'link.json').read_text(encoding='utf-8'))
    if recipe['schema']!='wa2.native-link-inputs.v1' or recipe['target']!=args.target:raise ValueError('Unexpected native link recipe')
    flags=0
    if os.name=='nt':
        kernel=ctypes.WinDLL('kernel32',use_last_error=True);kernel.GetCurrentProcess.restype=ctypes.c_void_p
        kernel.SetPriorityClass.argtypes=[ctypes.c_void_p,ctypes.c_uint]
        kernel.GetProcessAffinityMask.argtypes=[ctypes.c_void_p,ctypes.POINTER(ctypes.c_size_t),ctypes.POINTER(ctypes.c_size_t)]
        kernel.SetProcessAffinityMask.argtypes=[ctypes.c_void_p,ctypes.c_size_t]
        process=kernel.GetCurrentProcess();current=ctypes.c_size_t();system=ctypes.c_size_t()
        if not kernel.SetPriorityClass(process,0x4000) or not kernel.GetProcessAffinityMask(process,ctypes.byref(current),ctypes.byref(system)) or not kernel.SetProcessAffinityMask(process,current.value&-current.value):raise ctypes.WinError()
        flags=subprocess.CREATE_NO_WINDOW|subprocess.BELOW_NORMAL_PRIORITY_CLASS
    def input_path(name):
        path=(base/name).resolve()
        if not path.is_relative_to(base) or not path.is_file() or path.suffix not in ('.a','.o','.obj','.res','.so'):raise ValueError('Invalid native relink input: '+name)
        return str(path)
    objects=[input_path(name) for name in recipe['objects']]
    libraries=[]
    for value in recipe['libraries']:
        if value.startswith('-'):
            if value!='-pthread' and not re.fullmatch(r'-l[A-Za-z0-9_+.-]+',value):raise ValueError('Unsupported system link input: '+value)
            libraries.append(value)
        else:libraries.append(input_path(value))
    if not objects or not libraries:raise ValueError('Empty native application link recipe')
    if args.target=='android':
        if not args.ndk:parser.error('--ndk is required for Android')
        host='windows-x86_64' if os.name=='nt' else 'darwin-x86_64' if os.uname().sysname=='Darwin' else 'linux-x86_64'
        toolchain=args.ndk/'toolchains/llvm/prebuilt'/host
        compiler=toolchain/'bin'/('clang++.exe' if os.name=='nt' else 'clang++')
        command=[str(compiler),'--target=aarch64-none-linux-android26','--sysroot='+str(toolchain/'sysroot'),
            '-shared','-fPIC','-Wl,--build-id=sha1','-Wl,--no-rosegment','-Wl,--gc-sections','-Wl,--no-undefined',
            '-Wl,-z,max-page-size=16384','-Wl,--exclude-libs,ALL','-Wl,-Bsymbolic',
            '-Wl,-soname,libwa2_native_android.so','-o',str(args.output),*objects,*libraries,'-nostdlib++','-lc++_shared']
    else:
        if not args.zig:parser.error('--zig is required for Windows')
        # The C++ driver does not accept -j1 and may spawn parallel MinGW
        # import-library builds even with a one-job Ninja parent. Link the
        # already compiled C++ objects through the job-limited Zig linker.
        command=[str(args.zig),'build-exe','-j1','-OReleaseSmall','-target','x86_64-windows-gnu','-lc','-lc++',
            '--subsystem','windows','-femit-bin='+str(args.output),*objects,*libraries]
    subprocess.run(command,check=True,creationflags=flags)


if __name__=='__main__':main()
