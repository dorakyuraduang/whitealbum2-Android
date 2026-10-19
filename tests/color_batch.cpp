// Compare the specialized native color function with the original generated
// instruction bodies, including private stack slots and partial fault state.
#include <winrecomp/compiled.hpp>
#include <winrecomp/core.hpp>
#include <winrecomp/hash.hpp>
#include "color_batch.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <random>
bool code_page_1083(wr::Cpu&,wr::Memory&,std::uint64_t&);
using namespace wr;
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(false)
struct Case {
    U32 index{1},element{2},alpha{255},red{128},green{255},blue{64},flag{},sp{0x07001000},flags{0x202},defined{STATUS_FLAGS},seed{},protect{},return_pc{0xffffffc0};
    unsigned permissions{Memory::Read},slice{4096},repeats{1};int corrupt{-1};std::uint64_t budget{1000};bool writable_code{};
};
struct Result {Cpu cpu;std::uint64_t budget{},instructions{},batches{};std::vector<std::uint8_t> bytes;std::string fault;double seconds{};};
Result execute(const std::vector<std::uint8_t>& code,const Case& c,bool fast){
    constexpr U32 begin=0x43b290,end=0x43b3bf;
    const std::array<std::pair<U32,unsigned>,3> regions={{{0x43b000,4096},{0x07000000,8192},{0xab4000,65536}}};
    Memory m;for(auto [base,size]:regions){m.map(base,size,Memory::Read|Memory::Write|(base==0x43b000?Memory::Execute:0));std::vector<std::uint8_t> bytes(size);for(unsigned i=0;i<size;++i)bytes[i]=std::uint8_t(i*31+(i>>3));m.copy_in(base,bytes);}m.initialize(0x43b000,code);
    if(!c.writable_code)m.protect(0x43b000,4096,Memory::Read|Memory::Execute);
    Cpu cpu;cpu.r={0x12345678,0x76543210,0xabcdef01,0x13579bdf,c.sp,0x2468ace0,0x1234abcd,0x19283746};cpu.flags=c.flags;cpu.defined_flags=c.defined;cpu.fs_base=0x1000;cpu.gs_base=0x2000;cpu.fs_valid=true;cpu.gs_valid=true;
    std::mt19937 random(c.seed);for(auto& byte:cpu.fp.bytes)byte=std::uint8_t(random());
    const auto setup=[&]{
        const U32 words[]={0x50000000,c.index,c.element,c.alpha,c.red,c.green,c.blue};
        for(unsigned i=0;i<7;++i){try{m.store(c.sp+i*4,words[i],32);}catch(const GuestFault&){} }
        try{m.store((c.index<<8)+0xab4002,c.flag,16);}catch(const GuestFault&){}
    };
    setup();if(c.corrupt>=0)m.initialize(begin+U32(c.corrupt),std::array<std::uint8_t,1>{0xcc});if(c.protect)m.protect(c.protect,4096,c.permissions);
    Result result;auto budget=c.budget;const auto start=std::chrono::steady_clock::now();
    try{
        for(unsigned iteration=0;iteration<c.repeats;++iteration){
            if(iteration)setup();cpu.eip=begin;cpu.r[ESP]=c.sp;
            const auto running=[&]{return cpu.eip>=begin && cpu.eip<end && cpu.eip!=c.return_pc;};
            while(running()){
                auto scope=m.execution_scope();unsigned count=0;
                while(count<c.slice && running()){
                    if(fast)if(auto n=wa2_color_batch(cpu,m,budget,c.slice-count,c.return_pc)){CHECK(n<=c.slice-count);count+=n;result.instructions+=n;++result.batches;continue;}
                    tick(budget,cpu.eip);CHECK(code_page_1083(cpu,m,budget));++count;++result.instructions;
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
    CHECK(argc==3);const auto image=Image::load(argv[1]);const auto page=image.code(0x43b000).first(4096);const std::vector<std::uint8_t> code(page.begin(),page.end());
    std::mt19937 random(0x43b290);unsigned cases=0;std::uint64_t batches=0;
    const auto verify=[&](const Case& c){const auto a=execute(code,c,false),b=execute(code,c,true);try{equal(a,b);}catch(...){std::cerr<<"Color mismatch case="<<cases<<" flag="<<c.flag<<" alpha="<<c.alpha<<" r="<<c.red<<" g="<<c.green<<" b="<<c.blue<<" slice="<<c.slice<<" budget="<<c.budget<<" instructions="<<a.instructions<<'/'<<b.instructions<<'\n';throw;}++cases;batches+=b.batches;};
    for(unsigned n=0;n<1500;++n){Case c;c.index=random()%200;c.element=random()%32;c.alpha=n%5?random()%512:random();c.red=n%5?random()%512:random();c.green=n%7?random()%512:random();c.blue=n%11?random()%512:random();c.flag=random();c.seed=random();c.flags=random();c.defined=random();c.slice=1+random()%160;c.budget=n%3?1000:random()%100;if(n%29==0)c.corrupt=int(random()%303);verify(c);}
    for(auto value:{0u,1u,127u,128u,254u,255u,256u,0x3fffffffu,0x40000000u,0x7fffffffu,0x80000000u,0xffffff80u,0xffffffffu})for(auto flag:{0u,0x7fffu,0x8000u,0xffffu}){Case c;c.alpha=c.red=c.green=c.blue=value;c.flag=flag;verify(c);}
    for(unsigned n=0;n<=82;++n){Case c;c.budget=n;verify(c);c.budget=1000;c.slice=n+1;verify(c);}
    for(auto address:{0x07000000u,0x07001000u,0xab4000u})for(auto permission:{unsigned(Memory::Read),unsigned(Memory::Write)}){Case c;c.protect=address;c.permissions=permission;verify(c);}
    for(auto sp:{0u,16u,0xfffffff0u,0x07000010u,0x07001ff0u,0xab4200u,0x43b2a0u}){Case c;c.sp=sp;c.writable_code=true;verify(c);}
    for(auto index:{255u,256u,0xffffffffu,0x80000000u}){Case c;c.index=index;verify(c);}
    for(auto element:{(0x07001003u-0xab400bu)/4,(0x43b2a3u-0xab400bu)/4,0xffffffffu}){Case c;c.index=0;c.element=element;c.writable_code=true;verify(c);}
    for(auto pc:{0x43b290u,0x43b2a0u,0x43b352u,0x43b395u,0x43b3beu}){Case c;c.return_pc=pc;verify(c);}
    CHECK(batches>0);Case benchmark;benchmark.repeats=100000;benchmark.budget=10000000;const auto before=execute(code,benchmark,false),after=execute(code,benchmark,true);equal(before,after);CHECK(after.fault.empty() && after.batches==benchmark.repeats);
    std::ofstream report(argv[2]);report<<"{\"schema\":\"wa2.color-batch-tests.v1\",\"cases\":"<<cases<<",\"exact_cpu_flags_fpu_memory_budget_and_faults\":true,\"calls\":"<<benchmark.repeats<<",\"guest_instructions\":"<<after.instructions<<",\"before_seconds\":"<<before.seconds<<",\"after_seconds\":"<<after.seconds<<",\"speedup\":"<<before.seconds/after.seconds<<",\"memory_sha256\":\""<<sha256(after.bytes)<<"\"}\n";CHECK(report);
    std::cout<<"PASS "<<cases<<" color equivalence cases; "<<benchmark.repeats<<" calls "<<before.seconds<<" -> "<<after.seconds<<" seconds ("<<before.seconds/after.seconds<<"x)\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
