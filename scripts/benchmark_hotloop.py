"""Time the unchanged, statically translated LZSS copy loop on fixed input.

The baseline uses the pinned upstream Memory/check_code headers. Both builds
execute the same generated instruction bodies. This is a test executable;
neither the harness nor the baseline is linked into the game.
"""
from pathlib import Path
import argparse
import hashlib
import json
import statistics
import subprocess
import sys
import zipfile

ROOT=Path(__file__).resolve().parent.parent
sys.path.insert(0,str(ROOT/'tools/python'))
import pefile

DRIVER=r'''
#include <chrono>
#include <fstream>
#include <iostream>
#include <optional>
#include "GENERATED_PAGE"
int main(int argc,char** argv){try{
    if(argc!=4)throw std::runtime_error("usage: benchmark code-page.bin iterations state.bin");
    const auto iterations=std::stoul(argv[2]);if(!iterations || iterations>64000000)throw std::runtime_error("invalid iteration count");
    std::ifstream file(argv[1],std::ios::binary);std::vector<std::uint8_t> code((std::istreambuf_iterator<char>(file)),{});
    if(code.size()!=4096)throw std::runtime_error("expected the original 4 KiB code page");
    wr::Memory memory;memory.map(0x437000,4096,wr::Memory::Read|wr::Memory::Execute);memory.initialize(0x437000,code);
    memory.map(0xa7c000,8192,wr::Memory::Read|wr::Memory::Write);memory.map(0x07000000,4096,wr::Memory::Read|wr::Memory::Write);
    memory.map(0x20000000,iterations+16,wr::Memory::Read|wr::Memory::Write);
    for(unsigned n=0;n<4096;++n)memory.store(0xa7cdd0+n,(n*37+(n>>3))&255,8);
    wr::Cpu cpu;cpu.eip=0x4370c0;cpu.r[wr::ESP]=0x07000100;cpu.r[wr::EDI]=0x20000000;
    cpu.r[wr::ESI]=0xfee;cpu.r[wr::EAX]=0x123;cpu.r[wr::EDX]=iterations-1;
    memory.store(cpu.r[wr::ESP]+0x2c,iterations,32);memory.store(cpu.r[wr::ESP]+0x20,0x12345678,32);
    std::uint64_t budget=std::uint64_t(iterations)*17+1,executed=0;
    const auto start=std::chrono::steady_clock::now();
    {
#if WA2_SCOPED
        auto scope=memory.execution_scope();
#endif
        while(cpu.eip!=0x4370f1){wr::tick(budget,cpu.eip);if(!code_page_1079(cpu,memory,budget))throw std::runtime_error("unexpected instruction");++executed;}
    }
    const auto seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    if(executed!=std::uint64_t(iterations)*17)throw std::runtime_error("different instruction count");
    std::ofstream state(argv[3],std::ios::binary);state.write(reinterpret_cast<const char*>(cpu.r.data()),32);
    for(auto value:{cpu.eip,cpu.flags,cpu.defined_flags})state.write(reinterpret_cast<const char*>(&value),4);
    state.write(reinterpret_cast<const char*>(cpu.fp.bytes.data()),cpu.fp.bytes.size());
    auto dump=[&](wr::U32 at,std::size_t size){std::vector<std::uint8_t> bytes(size);memory.copy_out(at,bytes);state.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());};
    dump(0xa7c000,8192);dump(0x07000000,4096);dump(0x20000000,iterations+16);state.close();if(!state)throw std::runtime_error("state output failed");
    std::cout<<"{\"seconds\":"<<seconds<<",\"guest_instructions\":"<<executed<<",\"output_bytes\":"<<iterations<<"}\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
'''

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--iterations',type=int,default=2000000)
    parser.add_argument('--repeats',type=int,default=3)
    args=parser.parse_args()
    if not 0<args.iterations<=64000000 or not 1<=args.repeats<=9:raise ValueError('Invalid benchmark size/repeats')
    out=ROOT/'build/performance-hotloop';out.mkdir(parents=True,exist_ok=True)
    overlay=out/'baseline-include/winrecomp';overlay.mkdir(parents=True,exist_ok=True)
    with zipfile.ZipFile(ROOT/'tools/winrecomp.zip') as archive:
        for name in ['memory.hpp','compiled.hpp']:
            matches=[p for p in archive.namelist() if p.endswith('/include/winrecomp/'+name)]
            if len(matches)!=1:raise ValueError('Ambiguous baseline header')
            (overlay/name).write_bytes(archive.read(matches[0]))
    driver=out/'driver.cpp';driver.write_text(DRIVER.replace('GENERATED_PAGE',(ROOT/'generated/wa2-win64/code_page_1079.cpp').as_posix()))
    image=pefile.PE(str(ROOT/'WA2_chs.exe'));mapped=image.get_memory_mapped_image();code=out/'code-page.bin'
    offset=0x437000-image.OPTIONAL_HEADER.ImageBase;code.write_bytes(mapped[offset:offset+4096])
    programs={}
    for name,scoped in [('before',False),('after',True)]:
        program=out/(name+'.exe')
        command=[str(ROOT/'scripts/zig-cxx.cmd'),'-std=c++20','-O2',f'-DWA2_SCOPED={int(scoped)}']
        if not scoped:command+=['-I',str(overlay.parent)]
        command+=['-I',str(ROOT/'tools/WindowsRecompTool-main/include'),str(driver),str(ROOT/'build/wa2-win64/winrecomp/libwinrecomp_native.a'),'-o',str(program)]
        result=subprocess.run(command,capture_output=True,text=True,timeout=180)
        (out/(name+'-compile.log')).write_text(result.stdout+result.stderr)
        if result.returncode:raise RuntimeError(result.stdout+result.stderr)
        programs[name]=program
    results={name:[] for name in programs};reference=None
    # Alternate order to reduce bias from CPU temperature and background work.
    for repeat in range(args.repeats):
        for name in (['before','after'] if repeat%2==0 else ['after','before']):
            state=out/(name+'-state.bin')
            result=subprocess.run([str(programs[name]),str(code),str(args.iterations),str(state)],capture_output=True,text=True,timeout=180)
            if result.returncode:raise RuntimeError(result.stdout+result.stderr)
            payload=state.read_bytes()
            if reference is None:reference=payload
            if payload!=reference:raise RuntimeError(f'{name}: CPU/FPU/memory differs from baseline')
            results[name].append(json.loads(result.stdout))
    medians={name:statistics.median(x['seconds'] for x in values) for name,values in results.items()}
    record={'schema':'wa2.hotloop-performance.v1','input_sha256':digest(ROOT/'WA2_chs.exe'),'loop_entry':'0x4370c0','loop_stop':'0x4370f1','iterations':args.iterations,'guest_instructions_per_run':17*args.iterations,'results':results,'median_seconds':medians,'speedup':round(medians['before']/medians['after'],3),'cpu_fpu_and_all_mapped_writable_bytes_identical':True,'state_sha256':hashlib.sha256(reference).hexdigest(),'translation_sha256':digest(ROOT/'generated/wa2-win64/code_page_1079.cpp'),'baseline_memory_sha256':digest(overlay/'memory.hpp'),'optimized_memory_sha256':digest(ROOT/'tools/WindowsRecompTool-main/include/winrecomp/memory.hpp'),'scope':'isolated resource-decompression copy loop; not whole-game timing'}
    (ROOT/'logs/performance-hotloop.json').write_text(json.dumps(record,indent=2)+'\n')
    print(json.dumps(record,indent=2))

if __name__=='__main__':main()
