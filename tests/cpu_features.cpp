// Exercise the original CRT's EFLAGS/CPUID/SSE2 probe without windows/input.
#include <winrecomp/process.hpp>
#include "native_backend.hpp"
#include <fstream>
#include <iostream>
bool code_page_1167(wr::Cpu&,wr::Memory&,std::uint64_t&);
bool code_page_1152(wr::Cpu&,wr::Memory&,std::uint64_t&);
bool code_page_1148(wr::Cpu&,wr::Memory&,std::uint64_t&);
using namespace wr;
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(false)
static bool step(Cpu& cpu,Memory& memory,std::uint64_t& budget){
    switch(cpu.eip>>12){case 0x48f:return code_page_1167(cpu,memory,budget);case 0x480:return code_page_1152(cpu,memory,budget);case 0x47c:return code_page_1148(cpu,memory,budget);default:return false;}
}
int main(int argc,char** argv){try{
    CHECK(argc==3);const auto image=Image::load(argv[1]);
    std::array<U32,3> results{};
    for(unsigned mode=0;mode<3;++mode){
        ProcessOptions options;options.instruction_budget=1000000;
        Process p(step,options);CHECK(p.memory.enable_linear_memory());p.load(image);
        constexpr U32 returned=0xffffffa0;
        const auto saved_sp=p.cpu.r[ESP];push(p.cpu,p.memory,returned);p.cpu.eip=0x48fb94;
        unsigned ticks=0;
        while(p.cpu.eip!=returned && ticks<10000){
            if(mode)if(auto n=wa2_native_batch(p.cpu,p.memory,p.budget,mode==2?1:1000,returned)){ticks+=n;continue;}
            tick(p.budget,p.cpu.eip);CHECK(step(p.cpu,p.memory,p.budget));++ticks;
        }
        CHECK(p.cpu.eip==returned && p.cpu.r[ESP]==saved_sp);
        results[mode]=p.cpu.r[EAX];std::cout<<"mode "<<mode<<" result "<<results[mode]<<" steps "<<ticks<<'\n';
        CHECK(results[mode]==1);
    }
    {
        ProcessOptions options;options.instruction_budget=1000000;Process p(step,options);CHECK(p.memory.enable_linear_memory());p.load(image);
        const auto initial=p.cpu;constexpr U32 returned=0xffffffa0;
        for(auto entry:{0x48fb94u,0x47cb9cu,0x48fbf6u})for(unsigned limit=2;limit<=256;++limit){
            p.cpu=initial;p.budget=1000000;p.cpu.flags=0x256;p.cpu.r[ECX]=entry;p.cpu.r[EDX]=3;
            p.cpu.r[EBX]=0x4a13d4;p.cpu.r[ESI]=0x4a13cc;p.cpu.r[EDI]=0x4a13e8;
            p.cpu.r[ESP]-=(limit&3)*4;const auto saved_sp=p.cpu.r[ESP];
            p.memory.store(0xbc830c,0,32);p.memory.store(0xbc8310,0,32);push(p.cpu,p.memory,returned);p.cpu.eip=entry;
            unsigned ticks=0;
            while(p.cpu.eip!=returned && ticks<10000){
                if(auto n=wa2_native_batch(p.cpu,p.memory,p.budget,limit,returned)){ticks+=n;continue;}
                tick(p.budget,p.cpu.eip);CHECK(step(p.cpu,p.memory,p.budget));++ticks;
            }
            const auto result=entry==0x48fb94?p.cpu.r[EAX]:p.memory.load(entry==0x47cb9c?0xbc830c:0xbc8310,32);
            if(result!=1)std::cerr<<"CRT slice failure entry="<<std::hex<<entry<<std::dec<<" limit="<<limit<<" result="<<result<<'\n';
            CHECK(p.cpu.eip==returned && p.cpu.r[ESP]==saved_sp && result==1);
        }
    }
    std::ofstream report(argv[2]);report<<"{\"schema\":\"wa2.cpu-feature-tests.v1\",\"oracle\":"<<results[0]<<",\"native\":"<<results[1]<<",\"single_step\":"<<results[2]<<",\"slice_cases\":765,\"screen_access\":false,\"passed\":true}\n";CHECK(report);
    std::cout<<"PASS original CRT EFLAGS/CPUID/SSE2 probe in oracle, native and single-step modes; no windows or screen access\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
