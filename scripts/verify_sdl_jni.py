"""Check compiled SDL Java native declarations against the actual Android ELF.

No library is loaded and no Android process, emulator or screen is used.
"""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT=Path(__file__).resolve().parents[1]


def mangle(value):
    result=[];encoded=value.encode('utf-16-be')
    for at in range(0,len(encoded),2):
        unit=int.from_bytes(encoded[at:at+2],'big')
        c=chr(unit)
        if c.isascii() and c.isalnum():result.append(c)
        elif c=='/':result.append('_')
        elif c=='_':result.append('_1')
        elif c==';':result.append('_2')
        elif c=='[':result.append('_3')
        else:result.append('_0'+format(unit,'04x'))
    return ''.join(result)


def verify(classes,library):
    classes=Path(classes);library=Path(library)
    class_files=sorted((classes/'org/libsdl/app').glob('*.class'))
    if not class_files:raise ValueError('Compiled SDL Java classes are missing')
    flags=subprocess.CREATE_NO_WINDOW|subprocess.BELOW_NORMAL_PRIORITY_CLASS
    def run(command):
        return subprocess.run(list(map(str,command)),cwd=ROOT,creationflags=flags,capture_output=True,
            text=True,encoding='utf-8',errors='replace',check=True,timeout=30).stdout
    declarations=run([ROOT/'tools/source-analysis/jdk-21.0.12.1+1/bin/javap.exe','-p','-s',*class_files])
    nm=ROOT/'tools/android-sdk/ndk/27.0.12077973/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-nm.exe'
    symbols={line.split()[-1] for line in run([nm,'-D','--defined-only',library]).splitlines() if line.split()}
    owner=None;pending=None;methods=[];missing=[]
    for line in declarations.splitlines():
        declaration=re.search(r'\b(?:class|interface)\s+([\w.$]+)',line)
        if declaration:owner=declaration[1];pending=None
        method=re.match(r'^\s+.*\bnative\b.*\s([\w$]+)\(',line)
        if method:
            if owner is None or pending is not None:raise ValueError('Unrecognized javap native method declaration')
            pending=(owner,method[1])
        descriptor=re.match(r'^\s+descriptor:\s+(\(.*\).+)$',line)
        if descriptor and pending:
            class_name,name=pending;signature=descriptor[1];pending=None
            short='Java_'+mangle(class_name.replace('.','/'))+'_'+mangle(name)
            long=short+'__'+mangle(signature[1:signature.index(')')])
            found=short if short in symbols else long if long in symbols else None
            record=dict(owner=class_name,method=name,descriptor=signature,symbol=found)
            methods.append(record)
            if found is None:missing.append(dict(record,expected=[short,long]))
    if pending or len(methods)<50:raise ValueError('SDL JNI declaration inventory is incomplete')
    result=dict(schema='wa2.android-sdl-jni.v1',file=str(library),
        sha256=hashlib.sha256(library.read_bytes()).hexdigest(),native_methods=len(methods),methods=methods,
        missing=missing,game_launched=False,screen_access=False,android_device_verified=False,passed=not missing)
    if missing:
        raise ValueError('SDL Java native implementations are absent: '+', '.join(x['owner']+'.'+x['method'] for x in missing))
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--classes',type=Path,default=ROOT/'build/android-java')
    parser.add_argument('--library',type=Path,default=ROOT/'build/android-native-game/sdl2/libSDL2.so')
    parser.add_argument('--output',type=Path,default=ROOT/'logs/android-sdl-jni-verification.json')
    args=parser.parse_args()
    kernel=ctypes.WinDLL('kernel32');kernel.GetCurrentProcess.restype=ctypes.c_void_p
    kernel.SetPriorityClass.argtypes=[ctypes.c_void_p,ctypes.c_uint];kernel.SetPriorityClass(kernel.GetCurrentProcess(),0x4000)
    result=verify(args.classes,args.library)
    args.output.write_text(json.dumps(result,indent=2)+'\n')
    print('PASS: '+str(result['native_methods'])+' compiled SDL Java native declarations have Android ELF exports; no game was started.')


if __name__=='__main__':main()
