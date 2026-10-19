"""Enumerate only cached, non-retired x87 forms; never read the source EXE."""
from pathlib import Path
import json
import re

ROOT=Path(__file__).resolve().parents[1]
CASE=re.compile(r'^case 0x([0-9a-f]+)u: \{ (.*?)\nreturn true; \}',re.M|re.S)


def main():
    native=json.loads((ROOT/'port/native_functions.json').read_text())['functions']
    retired=json.loads((ROOT/'port/retired_modules.json').read_text())['modules']
    ranges=[(f['start'],f['end']) for f in native]+[(int(m['start'],0),int(m['end'],0)) for m in retired]
    forms={};simd={}
    for page in (ROOT/'generated/wa2-win64').glob('code_page_*.cpp'):
        for case in CASE.finditer(page.read_text()):
            pc=int(case[1],16)
            if any(a<=pc<b for a,b in ranges):continue
            for match in re.finditer(r'\bwr_x87_(\d+)_(m|r)(\d+)\b',case[2]):
                forms[match[0]]=(int(match[1]),int(match[3]),match[2]=='m')
            for match in re.finditer(r'\bwr_sse_(\d+)_(\d+)_(m|r)(\d+)(?:_i(\d+))?\b',case[2]):
                simd[match[0]]=(int(match[1]),int(match[2]),int(match[4]),match[3]=='m',int(match[5] or 0),False)
            for match in re.finditer(r'\bwr_sse_to_i32_(\d+)_(m|r)(\d*)\b',case[2]):
                simd[match[0]]=(0,int(match[1]),int(match[3] or 0),match[2]=='m',0,True)
    out=ROOT/'build/fp-kernel-input';out.mkdir(parents=True,exist_ok=True)
    lines=['#include "fp_registers.hpp"','#include "fp_kernel_cases.hpp"',
           'template<unsigned Opcode,unsigned Encoding,bool Memory>',
           'void register_kernel(void* state,void* operand,std::uint32_t*){',
           'wa2::aot::fp::detail::RegisterStack stack;stack.load(*static_cast<wr::FpState*>(state));stack.begin();',
           'wa2::aot::fp::x87_operation<Opcode,Encoding,Memory>(stack,operand);stack.finish();stack.save(*static_cast<wr::FpState*>(state));}']
    for name in sorted(forms):lines+=['#if WA2_FP_HOST_ORACLE',f'extern "C" void {name}(void*,void*,std::uint32_t*);','#endif']
    lines+=['namespace {','const FpKernelCase cases[]={']
    for name,(opcode,encoding,memory) in sorted(forms.items()):
        lines += ['{',f'"{name}", {opcode}, {encoding}, '+('true,' if memory else 'false,'),
                  f'wa2::aot::fp::x87<{opcode},{encoding},{str(memory).lower()}>,',
                  '#if WA2_FP_HOST_ORACLE',name+',','#else','nullptr,','#endif',
                  f'register_kernel<{opcode},{encoding},{str(memory).lower()}>,','},']
    lines+=['};','}','std::span<const FpKernelCase> fp_kernel_cases(){return cases;}']
    content='\n'.join(lines)+'\n';path=out/'cases.cpp'
    if not path.exists() or path.read_text()!=content:path.write_text(content)
    lines=['#include "sse_kernel.hpp"','#include "fp_kernel_cases.hpp"']
    for name in sorted(simd):lines+=['#if WA2_FP_HOST_ORACLE',f'extern "C" void {name}(void*,void*,std::uint32_t*);','#endif']
    lines+=['namespace {','const SimdKernelCase cases[]={']
    for name,(prefix,opcode,encoding,memory,immediate,conversion) in sorted(simd.items()):
        function=f'wa2::aot::fp::sse_to_i32<{opcode},{encoding},{str(memory).lower()}>' if conversion else f'wa2::aot::fp::sse<{prefix},{opcode},{encoding},{str(memory).lower()},{immediate}>'
        lines+=['{',f'"{name}",{prefix},{opcode},{encoding},{str(memory).lower()},{immediate},',function+',',
                '#if WA2_FP_HOST_ORACLE',name+',','#else','nullptr,','#endif','},']
    lines+=['};','}','std::span<const SimdKernelCase> simd_kernel_cases(){return cases;}']
    content='\n'.join(lines)+'\n';path=out/'simd_cases.cpp'
    if not path.exists() or path.read_text()!=content:path.write_text(content)
    (out/'inventory.json').write_text(json.dumps(dict(forms=len(forms),simd_forms=len(simd),helpers=sorted(forms),simd_helpers=sorted(simd),source_executable_opened=False),indent=2)+'\n')
    print('Cached x87 forms:',len(forms),'SIMD:',len(simd))


if __name__=='__main__':main()
