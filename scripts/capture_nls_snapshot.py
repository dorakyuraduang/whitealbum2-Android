"""Capture OS character metadata for synthetic Unicode buffers only.

No source EXE, game resource, process, window, input device or screen is used.
These data-only ranges let the shared host answer the same NLS character
queries without calling a Windows DLL on Android.
"""
import ctypes as c
import hashlib
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'generated/codepages'


def main():
    kernel=c.WinDLL('kernel32',use_last_error=True)
    kernel.GetCurrentProcess.restype=c.c_void_p;kernel.SetPriorityClass.argtypes=[c.c_void_p,c.c_uint]
    if not kernel.SetPriorityClass(kernel.GetCurrentProcess(),0x4000):raise c.WinError(c.get_last_error())
    get_type=kernel.GetStringTypeW;get_type.argtypes=[c.c_uint,c.c_void_p,c.c_int,c.c_void_p];get_type.restype=c.c_int
    mapping=kernel.LCMapStringW;mapping.argtypes=[c.c_uint,c.c_uint,c.c_void_p,c.c_int,c.c_void_p,c.c_int];mapping.restype=c.c_int
    source=(c.c_uint16*65536)(*range(65536));kinds=[]
    for kind in (1,2,4):
        output=(c.c_uint16*65536)()
        if not get_type(kind,source,65536,output):raise c.WinError(c.get_last_error())
        kinds.append(list(output))
    rows=[]
    for scalar in range(65536):
        values=tuple(table[scalar] for table in kinds)
        if rows and rows[-1][1]+1==scalar and tuple(rows[-1][2:])==values:rows[-1][1]=scalar
        else:rows.append([scalar,scalar,*values])
    cases={}
    for name,flag in [('lower',0x100),('upper',0x200)]:
        output=(c.c_uint16*131072)();count=mapping(0x804,flag,source,65536,output,len(output))
        if count!=65536:raise ValueError('bulk scalar case mapping does not preserve one UTF16 unit per input; capture requires a stronger contract')
        cases[name]=[(scalar,output[scalar]) for scalar in range(65536) if scalar!=output[scalar]]
        # Check isolated values too. A contextual map must not be admitted as
        # a context-free per-scalar table just because bulk lengths match.
        for scalar,value in cases[name]:
            one=(c.c_uint16*1)(scalar);mapped=(c.c_uint16*4)()
            if mapping(0x804,flag,one,1,mapped,4)!=1 or mapped[0]!=value:raise ValueError('contextual case mapping at '+hex(scalar))
    text=['// Local Windows NLS responses to synthetic BMP buffers. Data only.',
          '#pragma once','#include <cstdint>','#include <span>','namespace wa2::platform::compat {',
          'struct NlsRange {std::uint16_t first,last,ctype1,ctype2,ctype3;};',
          'inline constexpr NlsRange scalar_ranges[]={']
    text += ['{'+','.join(str(v) for v in row)+'},' for row in rows];text+=['};','struct NlsCase {std::uint16_t from,to;};']
    for name,values in cases.items():
        text+=['inline constexpr NlsCase scalar_'+name+'[]={']
        text+=['{'+str(a)+','+str(b)+'},' for a,b in values];text+=['};']
    text+=['}'];content='\n'.join(text)+'\n';OUT.mkdir(parents=True,exist_ok=True);(OUT/'nls_snapshot.hpp').write_text(content,encoding='utf-8',newline='\n')
    report=dict(schema='wa2.common-nls-snapshot.v1',source='Local Windows NLS responses to synthetic BMP buffers',
                scalar_count=65536,classification_ranges=len(rows),case_mappings={k:len(v) for k,v in cases.items()},
                locale=0x804,contextual_case_mappings_admitted=False,sha256=hashlib.sha256(content.encode()).hexdigest(),
                source_executable_opened=False,resources_opened=False,screen_access=False,game_launched=False)
    (OUT/'nls_snapshot.json').write_text(json.dumps(report,indent=2)+'\n')
    print('Captured common BMP character metadata:',len(rows),'ranges;',sum(map(len,cases.values())),'case mappings.')


if __name__=='__main__':main()
