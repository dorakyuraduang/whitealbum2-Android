"""Generate data-only C++ character maps from the bundled Python codecs.

No binary input, resource file or game process is opened. The resulting maps
are shared by Windows/ARM64; Android does not need a Windows NLS service.
"""
import ctypes
import json
import os
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'generated/codepages'


def write(name,text):
    path=OUT/name
    if not path.exists() or path.read_text()!=text:path.write_text(text,encoding='utf-8',newline='\n')


def main():
    if os.name=='nt':
        kernel=ctypes.WinDLL('kernel32',use_last_error=True);kernel.GetCurrentProcess.restype=ctypes.c_void_p
        kernel.SetPriorityClass.argtypes=[ctypes.c_void_p,ctypes.c_uint]
        if not kernel.SetPriorityClass(kernel.GetCurrentProcess(),0x4000):raise ctypes.WinError(ctypes.get_last_error())
    OUT.mkdir(parents=True,exist_ok=True)
    cpp=['// Data-only tables from the bundled standard-library codec maps.',
         '#include "../../platform/compat/codepage.hpp"','namespace wa2::platform::compat {','namespace {']
    report=[]
    for cp in (437,1252,932,936):
        singles=[];pairs=[]
        for byte in range(256):
            try:value=ord(bytes([byte]).decode('cp'+str(cp)))
            except UnicodeError:value=byte if cp==1252 else 0xffff
            # Windows CP936 assigns the single-byte euro at 0x80.
            if cp==936 and byte==128:value=0x20ac
            singles.append(value)
        if cp in (932,936):
            for high in range(128,256):
                if singles[high]!=0xffff:continue
                for low in range(256):
                    try:decoded=bytes([high,low]).decode('cp'+str(cp))
                    except UnicodeError:continue
                    if len(decoded)==1 and ord(decoded)!=0xffff:pairs.append(((high<<8|low)<<16)|ord(decoded))
        reverse=[]
        for scalar in range(65536):
            try:encoded=chr(scalar).encode('cp'+str(cp))
            except UnicodeError:continue
            if 0<len(encoded)<=2:reverse.append((scalar<<16)|int.from_bytes(encoded,'big'))
        if cp==936:reverse=[x for x in reverse if x>>16!=0x20ac]+[(0x20ac<<16)|0x80];reverse.sort()
        if cp==1252:
            present={x>>16 for x in reverse}
            reverse += [(scalar<<16)|byte for byte,scalar in enumerate(singles) if scalar not in present]
            reverse.sort()
        for name,ctype,values in [('single','std::uint16_t',singles),('pairs','std::uint32_t',pairs),('reverse','std::uint32_t',reverse)]:
            if not values:continue
            cpp.append(f'constexpr {ctype} {name}_{cp}[]={{')
            cpp.extend(','.join(hex(x)+'u' for x in values[start:start+16])+',' for start in range(0,len(values),16))
            cpp.append('};')
        report.append(dict(codepage=cp,single_bytes=sum(x!=0xffff for x in singles),double_bytes=len(pairs),encodable_scalars=len(reverse)))
    cpp+=['}','const Codepage* codepage(std::uint32_t cp) noexcept {','static const Codepage pages[]={']
    for row in report:
        cp=row['codepage'];cpp.append(f'{{{cp}u,single_{cp},'+(f'pairs_{cp}' if row['double_bytes'] else '{}')+f',reverse_{cp}'+'},')
    cpp+=['};for(const auto& page:pages)if(page.number==cp)return &page;return nullptr;}','}']
    write('tables.cpp','\n'.join(cpp)+'\n');write('manifest.json',json.dumps(dict(schema='wa2.common-codepages.v1',maps=report,source_executable_opened=False),indent=2)+'\n')
    print('Generated common data-only codepage maps for 437, 1252, 932 and 936.')


if __name__=='__main__':main()
