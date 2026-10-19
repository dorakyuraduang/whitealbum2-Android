#include <winrecomp/compiled.hpp>
#include <winrecomp/core.hpp>
#include <winrecomp/hash.hpp>
#include "vertex_batch.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <random>
bool code_page_1083(wr::Cpu&,wr::Memory&,std::uint64_t&);
bool code_page_1084(wr::Cpu&,wr::Memory&,std::uint64_t&);
using namespace wr;
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(false)
struct Case {
    U32 mode{},output_flag{},vertex{},sp{0x07001000},source{0x22000000},initial_palette{0x30},palette{},flags{0x202},defined{STATUS_FLAGS},seed{},protect{},return_pc{0xffffffc0};
    unsigned permissions{Memory::Read},slice{4096},repeats{1};int corrupt{-1},fixed_palette{-1},fixed_source{-1};std::uint64_t budget{10000};bool writable_code{};
};
struct Result {Cpu cpu;std::uint64_t budget{},instructions{},batches{};std::vector<std::uint8_t> bytes;std::string fault;double seconds{};};
Result execute(const std::vector<std::uint8_t>& code,const Case& c,bool fast){
    constexpr U32 begin=0x43bde0,end=0x43c100;
    const std::array<std::pair<U32,unsigned>,4> regions={{{0x43b000,8192},{0x07000000,8192},{0x22000000,8192},{0x4c2000,4096}}};
    Memory m;std::mt19937 random(c.seed);for(auto [base,size]:regions){m.map(base,size,Memory::Read|Memory::Write|(base==0x43b000?Memory::Execute:0));std::vector<std::uint8_t> bytes(size);for(auto& byte:bytes)byte=std::uint8_t(random());if(base==0x22000000 && c.fixed_source>=0)std::fill(bytes.begin(),bytes.end(),std::uint8_t(c.fixed_source));if(base==0x4c2000 && c.fixed_palette>=0)std::fill(bytes.begin(),bytes.end(),std::uint8_t(c.fixed_palette));m.copy_in(base,bytes);}m.initialize(0x43b000,code);
    if(!c.writable_code)m.protect(0x43b000,8192,Memory::Read|Memory::Execute);
    Cpu cpu;cpu.r={0x12345678,0x76543210,c.initial_palette,0x13579bdf,c.sp,0x2468ace0,0x1234abcd,c.vertex};cpu.flags=c.flags;cpu.defined_flags=c.defined;cpu.fs_base=0x1000;cpu.gs_base=0x2000;cpu.fs_valid=true;cpu.gs_valid=true;for(auto& byte:cpu.fp.bytes)byte=std::uint8_t(random());
    const auto setup=[&]{for(auto [offset,value]:{std::pair<U32,U32>{0x10,c.mode},{0x14,c.output_flag},{0x48,c.source},{0x2c,c.palette}}){try{m.store(c.sp+offset,value,32);}catch(const GuestFault&){} }};
    setup();if(c.corrupt>=0)m.initialize(begin+U32(c.corrupt),std::array<std::uint8_t,1>{0xcc});if(c.protect)m.protect(c.protect,4096,c.permissions);
    Result result;auto budget=c.budget;const auto start=std::chrono::steady_clock::now();
    try{
        for(unsigned iteration=0;iteration<c.repeats;++iteration){
            if(iteration)setup();cpu.eip=begin;cpu.r[ESP]=c.sp;cpu.r[EDI]=c.vertex;cpu.r[EDX]=c.initial_palette;
            const auto running=[&]{return cpu.eip>=begin && cpu.eip<end && cpu.eip!=c.return_pc;};
            while(running()){
                auto scope=m.execution_scope();unsigned count=0;
                while(count<c.slice && running()){
                    if(fast)if(auto n=wa2_vertex_batch(cpu,m,budget,c.slice-count,c.return_pc)){CHECK(n<=c.slice-count);count+=n;result.instructions+=n;++result.batches;continue;}
                    tick(budget,cpu.eip);CHECK(cpu.eip<0x43c000?code_page_1083(cpu,m,budget):code_page_1084(cpu,m,budget));++count;++result.instructions;
                }
            }
        }
    }catch(const GuestFault& f){result.fault=std::to_string(int(f.kind))+":"+std::to_string(f.address);}
    result.seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();result.cpu=cpu;result.budget=budget;
    for(auto [base,size]:regions){m.protect(base,size,Memory::Read);const auto offset=result.bytes.size();result.bytes.resize(offset+size);m.copy_out(base,std::span(result.bytes).subspan(offset,size));}return result;
}
void equal(const Result& a,const Result& b){
    CHECK(a.cpu.r==b.cpu.r && a.cpu.eip==b.cpu.eip && a.cpu.flags==b.cpu.flags && a.cpu.defined_flags==b.cpu.defined_flags);
    CHECK(a.cpu.fp.bytes==b.cpu.fp.bytes && a.cpu.selectors==b.cpu.selectors && a.cpu.fs_base==b.cpu.fs_base && a.cpu.gs_base==b.cpu.gs_base && a.cpu.fs_valid==b.cpu.fs_valid && a.cpu.gs_valid==b.cpu.gs_valid);
    CHECK(a.budget==b.budget && a.instructions==b.instructions && a.bytes==b.bytes && a.fault==b.fault);
}
int main(int argc,char** argv){try{
    CHECK(argc==3);const auto image=Image::load(argv[1]);const auto page=image.code(0x43b000).first(8192);const std::vector<std::uint8_t> code(page.begin(),page.end());
    std::mt19937 random(0x43bde0);unsigned cases=0;std::uint64_t batches=0;
    const auto verify=[&](const Case& c){const auto a=execute(code,c,false),b=execute(code,c,true);try{equal(a,b);}catch(...){std::cerr<<"Vertex mismatch case="<<cases<<" mode="<<c.mode<<" flag="<<c.output_flag<<" vertex="<<c.vertex<<" seed="<<c.seed<<" slice="<<c.slice<<" budget="<<c.budget<<" instructions="<<a.instructions<<'/'<<b.instructions<<" fault="<<a.fault<<'/'<<b.fault<<'\n';for(unsigned i=0;i<8;++i)if(a.cpu.r[i]!=b.cpu.r[i])std::cerr<<"register"<<i<<'='<<std::hex<<a.cpu.r[i]<<'/'<<b.cpu.r[i]<<std::dec<<'\n';for(unsigned i=0;i<a.bytes.size();++i)if(a.bytes[i]!=b.bytes[i]){std::cerr<<"byte"<<i<<'='<<unsigned(a.bytes[i])<<'/'<<unsigned(b.bytes[i])<<'\n';break;}throw;}++cases;batches+=b.batches;};
    for(unsigned n=0;n<1200;++n){Case c;c.mode=random()%2;c.output_flag=random()%2;c.vertex=(random()%4)*4;c.initial_palette=random()%200;c.palette=random()%200;c.seed=random();c.flags=random();c.defined=random();c.slice=1+random()%900;c.budget=n%3?10000:random()%800;if(n%29==0)c.corrupt=int(random()%800);verify(c);}
    for(auto mode:{0u,1u,2u})for(auto flag:{0u,1u,2u})for(auto palette:{0,1,127,128,129,254,255})for(auto source:{0,1,127,128,254,255}){Case c;c.mode=mode;c.output_flag=flag;c.fixed_palette=palette;c.fixed_source=source;verify(c);}
    for(unsigned n=0;n<=180;++n){Case c;c.budget=n;verify(c);c.budget=10000;c.slice=n+1;verify(c);}
    for(auto address:{0x07001000u,0x22000000u,0x4c2000u})for(auto permission:{unsigned(Memory::Read),unsigned(Memory::Write)}){Case c;c.protect=address;c.permissions=permission;verify(c);}
    for(auto sp:{0u,0xfffffff0u,0x07001fc0u,0x22000100u,0x4c2200u,0x43bdf0u}){Case c;c.sp=sp;c.writable_code=true;verify(c);}
    for(auto source:{0x22001fe8u,0x22001ffcu,0xfffffff0u,0x07001010u,0x43bdf0u}){Case c;c.source=source;verify(c);}
    for(auto offset:{0xffffffffu,0x07001000u-0x4c2290u,0x4c2fffu-0x4c2290u}){Case c;c.initial_palette=offset;verify(c);c.palette=offset;verify(c);}
    for(auto vertex:{1u,15u,16u,17u,0xfffffffcu}){Case c;c.vertex=vertex;verify(c);}
    for(auto pc:{0x43bde0u,0x43beabu,0x43bf08u,0x43c030u,0x43c0fau}){Case c;c.return_pc=pc;verify(c);}
    CHECK(batches>0);std::array<Result,2> before,after;
    for(unsigned mode=0;mode<2;++mode){Case c;c.mode=mode;c.repeats=100000;c.budget=100000000;c.seed=12345;before[mode]=execute(code,c,false);after[mode]=execute(code,c,true);equal(before[mode],after[mode]);CHECK(after[mode].fault.empty() && after[mode].batches==c.repeats);}
    std::ofstream report(argv[2]);report<<"{\"schema\":\"wa2.vertex-batch-tests.v1\",\"cases\":"<<cases<<",\"exact_cpu_flags_fpu_memory_budget_and_faults\":true,\"benchmarks\":[";
    for(unsigned mode=0;mode<2;++mode){if(mode)report<<',';report<<"{\"mode\":"<<mode<<",\"quads\":100000,\"guest_instructions\":"<<after[mode].instructions<<",\"before_seconds\":"<<before[mode].seconds<<",\"after_seconds\":"<<after[mode].seconds<<",\"speedup\":"<<before[mode].seconds/after[mode].seconds<<",\"memory_sha256\":\""<<sha256(after[mode].bytes)<<"\"}";}report<<"]}\n";CHECK(report);
    std::cout<<"PASS "<<cases<<" vertex equivalence cases; mode0 "<<before[0].seconds<<" -> "<<after[0].seconds<<" seconds ("<<before[0].seconds/after[0].seconds<<"x); mode1 "<<before[1].seconds<<" -> "<<after[1].seconds<<" seconds ("<<before[1].seconds/after[1].seconds<<"x)\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
