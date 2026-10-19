// Compare the optimization with the already generated C++ instruction page.
// The fixture comes from port/native_backend_data.inc, without opening an EXE.
#include <winrecomp/compiled.hpp>
#include <winrecomp/core.hpp>
#include <winrecomp/hash.hpp>
#include "fast_paths.hpp"
#include <algorithm>
#include <bit>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <random>
#include <string_view>

bool code_page_1090(wr::Cpu&,wr::Memory&,std::uint64_t&);
using namespace wr;
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(false)
struct Result {
    Cpu cpu;std::uint64_t budget{},instructions{},batches{};
    std::vector<std::uint8_t> bytes;std::string fault;
};
void equal(const Result& a,const Result& b){
    CHECK(a.cpu.r==b.cpu.r && a.cpu.eip==b.cpu.eip && a.cpu.flags==b.cpu.flags && a.cpu.defined_flags==b.cpu.defined_flags);
    CHECK(a.cpu.fp.bytes==b.cpu.fp.bytes && a.cpu.selectors==b.cpu.selectors);
    CHECK(a.cpu.fs_base==b.cpu.fs_base && a.cpu.gs_base==b.cpu.gs_base && a.cpu.fs_valid==b.cpu.fs_valid && a.cpu.gs_valid==b.cpu.gs_valid);
    CHECK(a.budget==b.budget && a.instructions==b.instructions && a.bytes==b.bytes && a.fault==b.fault);
}
struct ExistingPage {
    std::array<std::uint8_t,4096> bytes{};
    std::span<const std::uint8_t> code(U32 address)const{CHECK(address==0x442000);return bytes;}
};
#include "sort_objects.hpp"

struct Benchmark {
    Memory memory;
    std::vector<std::uint8_t> stack=std::vector<std::uint8_t>(16384);
    static constexpr U32 sp=0x07000100,objects=0xab3fb9;
    Benchmark(const ExistingPage& page,unsigned active){
        CHECK(memory.enable_linear_memory());
        memory.map(0x442000,4096,Memory::Read|Memory::Execute);memory.initialize(0x442000,page.bytes);
        memory.map(0x4c2000,4096,Memory::Read|Memory::Write);memory.store(0x4c22b0,1,32);
        memory.map(0x07000000,16384,Memory::Read|Memory::Write);
        memory.map(0xab3000,0x81000,Memory::Read|Memory::Write);
        std::vector<std::uint8_t> sprites(0x81000);
        std::mt19937 random(0x442290);
        for(unsigned j=0;j<2048;++j){
            const auto offset=objects-0xab3000+j*256;
            if(j<active){
                sprites[offset-1]=sprites[offset]=1;
                const auto key=std::uint16_t(random()%30000);
                std::memcpy(sprites.data()+offset+1,&key,2);sprites[offset+3]=1;
            }
            const std::uint16_t sentinel=30000;
            std::memcpy(stack.data()+sp-0x07000000+0x438+j*2,&sentinel,2);
        }
        memory.initialize(0xab3000,sprites);
        // Prime code proofs before measuring the loop.
        memory.initialize(0x07000000,stack);memory.store(sp+0x1c,objects,32);
        Cpu prime;prime.eip=0x442290;prime.r[EAX]=objects;prime.r[EBX]=0xffffffffu;prime.r[ESP]=sp;
        auto budget=std::uint64_t(1000000000);CHECK(wa2_static_batch(prime,memory,budget,131072,0xfffffff0)>0);
    }
    double run(unsigned frames,unsigned slice){
        std::chrono::nanoseconds elapsed{};
        for(unsigned frame=0;frame<frames;++frame){
            memory.initialize(0x07000000,stack);memory.store(sp+0x1c,objects,32);
            Cpu cpu;cpu.eip=0x442290;cpu.r={objects,0,0,0xffffffffu,sp,0,0,0};
            auto budget=std::uint64_t(1000000000);
            const auto started=std::chrono::steady_clock::now();
            while(cpu.eip>=0x442290 && cpu.eip<0x442374){
                auto scope=memory.execution_scope();unsigned count=0;
                while(count<slice && cpu.eip>=0x442290 && cpu.eip<0x442374){
                    if(const auto n=wa2_static_batch(cpu,memory,budget,slice-count,0xfffffff0)){CHECK(n<=slice-count);count+=n;continue;}
                    tick(budget,cpu.eip);CHECK(code_page_1090(cpu,memory,budget));++count;
                }
            }
            elapsed+=std::chrono::steady_clock::now()-started;
            CHECK(cpu.r[ECX]==2048 && budget>0);
        }
        return double(elapsed.count())/1000000/frames;
    }
};
int main(int argc,char** argv){try{
    CHECK(argc==3 || (argc==4 && std::string_view(argv[3])=="--baseline"));ExistingPage page;
    const bool partial_search=argc==3;wa2_partial_sort_search(partial_search);
    {std::ifstream input(argv[1],std::ios::binary);CHECK(bool(input));input.read(reinterpret_cast<char*>(page.bytes.data()),page.bytes.size());CHECK(input.gcount()==4096);}
    unsigned cases=sort_object_test::run(page);
    for(unsigned index:{0u,31u,1023u,2047u})for(U32 key:{0x8000u,0xffffu,0u,30000u,32767u,0xc180u})for(U32 alignment:{0u,1u,2u,3u}){
        const sort_object_test::Input input{index,15,key,0x246,STATUS_FLAGS,0x07000100+alignment};
        equal(sort_object_test::execute(page,input,false),sort_object_test::execute(page,input,true));++cases;
    }
    // Stop immediately before and after the skipped search iterations. This
    // covers partial comparisons, the final branch, duplicate sentinels,
    // arbitrary incoming flags and unaligned legacy stack locations.
    std::mt19937 random(0x12044229);
    for(unsigned index:{0u,1u,31u,127u,1023u,2047u})for(unsigned position:{0u,1u,7u,31u}){
        const auto rank=std::min(position,index);
        const auto key=std::uint16_t(std::int16_t(-16000+int(rank)*16));
        for(unsigned boundary:{12u,13u,20u,12+9*rank,17+9*rank,18+9*rank,19+9*rank}){
            sort_object_test::Input input{index,15,key,random(),random(),0x07000100+(random()%4),4096,boundary};
            equal(sort_object_test::execute(page,input,false),sort_object_test::execute(page,input,true));++cases;
        }
    }
    for(unsigned n=0;n<150;++n){
        sort_object_test::Input input{random()%2048,15,random(),random(),random(),0x07000100+(random()%4),12+random()%4096,random()%50000};
        equal(sort_object_test::execute(page,input,false),sort_object_test::execute(page,input,true));++cases;
    }
    std::ofstream out(argv[2]);out<<"{\"schema\":\"wa2.existing-sort.v1\",\"partial_search_enabled\":"<<(partial_search?"true":"false")<<",\"cases\":"<<cases
        <<",\"generated_instruction_oracle\":true,\"original_executable_opened\":false,\"exact_cpu_flags_fpu_memory_budget_faults\":true,\"benchmarks\":[";
    bool first=true;
    for(unsigned active:{24u,128u,2048u})for(unsigned slice:{4096u,32768u,131072u}){
        Benchmark benchmark(page,active);std::array<double,5> timings{};
        for(auto& value:timings)value=benchmark.run(30,slice);
        std::sort(timings.begin(),timings.end());
        if(!first)out<<',';first=false;
        out<<"{\"active_objects\":"<<active<<",\"slice\":"<<slice<<",\"median_ms_per_sort\":"<<timings[2]<<'}';
        std::cout<<"active="<<active<<" slice="<<slice<<" median_ms="<<timings[2]<<'\n';
    }
    out<<"],\"screen_access\":false,\"automated_input\":false,\"whole_game_performance_measured\":false}\n";
    std::cout<<"Passed "<<cases<<" generated-code differential cases\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
