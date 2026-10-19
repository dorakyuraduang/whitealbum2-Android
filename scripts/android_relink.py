"""Relink the supplied native application objects with replaceable FFmpeg libs.

Run from an extracted WA2-native-relink.zip using Python 3 and Android NDK r27:
  python android_relink.py --ndk /path/to/android-ndk-r27 --output libwa2_android.so
Replace ffmpeg/lib/*.a with compatible modified builds before relinking.
"""
import argparse
import os
from pathlib import Path
import subprocess


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ndk',required=True,type=Path)
    parser.add_argument('--output',required=True,type=Path)
    parser.add_argument('--materials',type=Path,default=Path(__file__).resolve().parent)
    args=parser.parse_args()
    host='windows-x86_64' if os.name=='nt' else 'darwin-x86_64' if os.uname().sysname=='Darwin' else 'linux-x86_64'
    toolchain=args.ndk/'toolchains/llvm/prebuilt'/host
    compiler=toolchain/'bin'/('clang++.exe' if os.name=='nt' else 'clang++')
    root=args.materials;objects=sorted((root/'application/entry').glob('*.o'))
    archives=sorted((root/'application/libs').glob('*.a'))+sorted((root/'ffmpeg/lib').glob('*.a'))
    if len(objects)!=3 or len(archives)<10:raise ValueError('Incomplete native application/link materials')
    command=[str(compiler),'--target=aarch64-none-linux-android26','--sysroot='+str(toolchain/'sysroot'),
        '-shared','-fPIC','-Os','-Wl,--gc-sections','-Wl,--no-undefined','-Wl,-z,max-page-size=16384',
        '-Wl,-soname,libwa2_android.so','-Wl,--exclude-libs,ALL','-Wl,-Bsymbolic','-o',str(args.output),
        *map(str,objects),'-Wl,--start-group',*map(str,archives),'-Wl,--end-group',
        str(root/'native/libSDL2.so'),'-llog','-landroid','-lm','-latomic','-nostdlib++','-lc++_shared']
    kwargs=dict(check=True)
    if os.name=='nt':kwargs['creationflags']=subprocess.BELOW_NORMAL_PRIORITY_CLASS|subprocess.CREATE_NO_WINDOW
    subprocess.run(command,**kwargs)


if __name__=='__main__':main()
