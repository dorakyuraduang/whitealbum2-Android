"""Preserve graphics protocol numbers/layouts from the installed compiler SDK.

The tiny console probe prints SDK constants only. It does not create a device,
window, game process or screen capture, and never opens a source game binary.
"""
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT=Path(__file__).resolve().parents[1]
SDK=ROOT/'tools/python/ziglang/lib/libc/include/any-windows-any'
OUT=ROOT/'generated/graphics-abi'
TYPES=['D3DADAPTER_IDENTIFIER9','D3DDISPLAYMODE','D3DINDEXBUFFER_DESC','D3DRASTER_STATUS',
       'D3DRECT','D3DSURFACE_DESC','D3DVERTEXBUFFER_DESC','D3DVERTEXELEMENT9','D3DVIEWPORT9',
       'D3DVSHADERCAPS2_0','D3DPSHADERCAPS2_0','D3DCAPS9']
ALIASES=['D3DFORMAT','D3DPOOL','D3DDEVTYPE','D3DRESOURCETYPE','D3DMULTISAMPLE_TYPE']


def write(path,text):
    if not path.exists() or path.read_text()!=text:path.write_text(text,encoding='utf-8',newline='\n')


def main():
    kernel=ctypes.WinDLL('kernel32');kernel.GetCurrentProcess.restype=ctypes.c_void_p;kernel.SetPriorityClass.argtypes=[ctypes.c_void_p,ctypes.c_uint]
    if not kernel.SetPriorityClass(kernel.GetCurrentProcess(),0x4000):raise ctypes.WinError(ctypes.get_last_error())
    headers={name:(SDK/name).read_text() for name in ('d3d9.h','d3d9types.h','d3d9caps.h')}
    renderer=(ROOT/'port/direct3d_sdl.cpp').read_text()
    names=sorted(set(re.findall(r'\bD3D[A-Z_0-9]+\b',renderer))-set(TYPES+ALIASES+['D3DMATRIX','D3D9','D3DPS_VERSION','D3DVS_VERSION']))
    guids=sorted(set(re.findall(r'\bIID_[A-Za-z0-9_]+\b',renderer))|{'IID_'+name for name in re.findall(r'\bQI\((IDirect\w+),',renderer)})
    build=ROOT/'build/graphics-abi';build.mkdir(parents=True,exist_ok=True);OUT.mkdir(parents=True,exist_ok=True)
    source=['#define INITGUID','#include <d3d9.h>','#include <cstdio>',
            'int main(int argc,char** argv){if(argc!=2)return 2;auto* f=std::fopen(argv[1],"wb");if(!f)return 3;std::fputs("{\\\"constants\\\":{",f);']
    for index,name in enumerate(names):source.append('std::fprintf(f,"'+(',' if index else '')+'\\\"'+name+'\\\":%u",unsigned('+name+'));')
    source.append('std::fputs("},\\\"guids\\\":{",f);')
    for index,name in enumerate(guids):
        source.append('std::fprintf(f,"'+(',' if index else '')+'\\\"'+name+'\\\":[%u,%u,%u",unsigned('+name+'.Data1),unsigned('+name+'.Data2),unsigned('+name+'.Data3));')
        source.append('for(auto b:'+name+'.Data4)std::fprintf(f,",%u",unsigned(b));std::fputc(\']\',f);')
    source+=['std::fputs("},\\\"sizes\\\":{",f);']
    for index,name in enumerate(TYPES):source.append('std::fprintf(f,"'+(',' if index else '')+'\\\"'+name+'\\\":%zu",sizeof('+name+'));')
    source+=['std::fputs("}}",f);return std::fclose(f)!=0;}']
    cpp=build/'sdk_values.cpp';write(cpp,'\n'.join(source)+'\n')
    executable=build/'sdk_values.exe';values=build/'values.json'
    environment=os.environ.copy();environment.update(TEMP=str(ROOT/'build/temp'),TMP=str(ROOT/'build/temp'),ZIG_GLOBAL_CACHE_DIR=str(ROOT/'build/zig-cache'),ZIG_LOCAL_CACHE_DIR=str(ROOT/'build/zig-local'))
    flags=subprocess.BELOW_NORMAL_PRIORITY_CLASS|subprocess.CREATE_NO_WINDOW
    with (ROOT/'logs/graphics-abi-build.log').open('wb') as log:
        for command in ([ROOT/'tools/python/ziglang/zig.exe','c++','-target','x86_64-windows-gnu','-std=c++20','-O0','-fno-sanitize=all',cpp,'-o',executable], [executable,values]):
            subprocess.run(list(map(str,command)),cwd=ROOT,env=environment,creationflags=flags,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=180)
    values_json=json.loads(values.read_text())
    output=['// Compiler SDK interface facts; no native Windows graphics API is called.', '#pragma once','#include <cstdint>','#include <array>',
            'namespace wa2::graphics::abi {','using DWORD=std::uint32_t;using LONG=std::int32_t;using HRESULT=std::int32_t;',
            'struct GUID {std::uint32_t Data1;std::uint16_t Data2,Data3;std::array<std::uint8_t,8> Data4;bool operator==(const GUID&)const=default;};',
            'constexpr bool IsEqualIID(const GUID& a,const GUID& b){return a==b;}',
            'struct RECT {std::int32_t left,top,right,bottom;};struct PALETTEENTRY {std::uint8_t peRed,peGreen,peBlue,peFlags;};',
            '#pragma pack(push,4)','struct LARGE_INTEGER {std::uint64_t QuadPart;};','struct D3DMATRIX {float m[4][4];};']
    output += ['using '+name+'=std::uint32_t;' for name in ALIASES]
    # These are serialized interface declarations, not native implementation
    # code. Keep the installed header notices with the generated declarations.
    for name,header in headers.items():
        notice=re.match(r'\s*(/\*[\s\S]*?\*/)',header)
        if notice:output+=['// Interface declaration source: '+name,notice[1]]
    replacements={'DWORD':'std::uint32_t','UINT':'std::uint32_t','WINBOOL':'std::int32_t','BOOL':'std::int32_t','INT':'std::int32_t','LONG':'std::int32_t','WORD':'std::uint16_t','BYTE':'std::uint8_t','MAX_DEVICE_IDENTIFIER_STRING':'512'}
    all_headers='\n'.join(headers.values())
    for name in TYPES:
        match=re.search(r'typedef\s+struct\s+_?'+name+r'\s*\{([\s\S]*?)\}\s*'+name+r'\b[^;]*;',all_headers)
        if not match:raise ValueError('missing SDK interface declaration '+name)
        body=match[1];body=re.sub(r'/\*[\s\S]*?\*/','',body)
        for old,new in replacements.items():body=re.sub(r'\b'+old+r'\b',new,body)
        output+=['struct '+name+' {'+body+'};']
        # Adapter ID's native C++ trailing padding differs from its serialized
        # source layout; all fields still have their documented fixed offsets.
        expected=1100 if name=='D3DADAPTER_IDENTIFIER9' else values_json['sizes'][name]
        output+=['static_assert(sizeof('+name+')=='+str(expected)+');']
    output+=['#pragma pack(pop)']
    for name,value in values_json['constants'].items():output+=['inline constexpr std::uint32_t '+name+'='+hex(value)+'u;']
    for name,(a,b,cc,*tail) in values_json['guids'].items():output+=['inline constexpr GUID '+name+'={'+f'{a}u,{b}u,{cc}u,'+'{'+','.join(str(x) for x in tail)+'}};']
    output+=['constexpr std::uint32_t D3DPS_VERSION(unsigned major,unsigned minor){return 0xffff0000u|(major<<8)|minor;}',
             'constexpr std::uint32_t D3DVS_VERSION(unsigned major,unsigned minor){return 0xfffe0000u|(major<<8)|minor;}',
             'inline constexpr HRESULT S_OK=0,E_FAIL=std::int32_t(0x80004005u),E_NOINTERFACE=std::int32_t(0x80004002u),E_OUTOFMEMORY=std::int32_t(0x8007000eu);','}']
    write(OUT/'source_abi.hpp','\n'.join(output)+'\n')
    report=dict(schema='wa2.graphics-source-abi.v1',constants=len(names),interface_ids=len(guids),
                sdk_headers={name:hashlib.sha256((SDK/name).read_bytes()).hexdigest() for name in headers},
                native_sdk_used_only_for_constant_probe=True,source_executable_opened=False,graphics_device_created=False,screen_access=False,game_launched=False)
    write(OUT/'manifest.json',json.dumps(report,indent=2)+'\n');print('Generated common graphics ABI declarations and',len(names),'SDK constants.')


if __name__=='__main__':main()
