"""Fetch the pinned Khronos offline validator; it never creates a GPU device."""
import ctypes
import hashlib
import io
import json
from pathlib import Path
import urllib.request
import zipfile

ROOT=Path(__file__).resolve().parents[1]
URL='https://github.com/KhronosGroup/glslang/releases/download/16.6.0/glslang-16.6.0-windows-x86_64-release.zip'
SHA256='82bf434e69b9bb4829de7e2b4bc2c5e7a7861e53d66cf75e5cc70f5f694a8d9b'


def main():
    kernel=ctypes.WinDLL('kernel32');kernel.GetCurrentProcess.restype=ctypes.c_void_p
    kernel.SetPriorityClass.argtypes=[ctypes.c_void_p,ctypes.c_uint];kernel.SetPriorityClass(kernel.GetCurrentProcess(),0x4000)
    destination=(ROOT/'tools/glslang/16.6.0').resolve();destination.mkdir(parents=True,exist_ok=True)
    request=urllib.request.Request(URL,headers={'User-Agent':'WA2-local-shader-validation'})
    with urllib.request.urlopen(request,timeout=45) as response:data=response.read(32*1024*1024+1)
    if len(data)>32*1024*1024 or hashlib.sha256(data).hexdigest()!=SHA256:raise ValueError('Khronos validator archive checksum/size mismatch')
    archive=destination/'official-release.zip';archive.write_bytes(data)
    with zipfile.ZipFile(io.BytesIO(data)) as package:
        candidates=[p for p in package.namelist() if Path(p).name.lower() in ('glslang.exe','glslangvalidator.exe')]
        if not candidates:raise ValueError('No standalone validator in the official archive')
        for item in package.infolist():
            path=(destination/item.filename).resolve()
            if not path.is_relative_to(destination):raise ValueError('Unsafe validator archive path')
            if item.is_dir():continue
            if path.suffix.lower() not in ('.exe','.dll','.txt') and 'license' not in path.name.lower():continue
            path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(package.read(item))
        binary=destination/candidates[0]
    record=dict(schema='wa2.offline-glsl-validator.v1',version='16.6.0',source_url=URL,archive_sha256=SHA256,
        file=binary.relative_to(ROOT).as_posix(),sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
        official_release_page='https://github.com/KhronosGroup/glslang/releases/expanded_assets/16.6.0',
        game_dependency=False,graphics_device_created=False,source_executable_opened=False)
    (destination/'tool.json').write_text(json.dumps(record,indent=2)+'\n');print(json.dumps(record,indent=2))


if __name__=='__main__':main()
