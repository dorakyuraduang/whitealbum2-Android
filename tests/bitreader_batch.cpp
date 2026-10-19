// Compare the complete bit lookup function with its single-instruction oracle.
#include "bitreader_batch.hpp"
#include <winrecomp/compiled.hpp>
#include <winrecomp/core.hpp>
#include <winrecomp/hash.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <random>

bool code_page_1183(wr::Cpu&,wr::Memory&,std::uint64_t&);
using namespace wr;
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(false)
namespace {
constexpr U32 begin=0x49fb20,end=0x49fbc7,stop=0xfffffff0;
constexpr std::array<std::pair<U32,unsigned>,5> regions={{{0x49f000,4096},{0x4d2000,4096},{0x07000000,8192},{0x20000000,8192},{0x21000000,8192}}};
struct Case {
    Cpu cpu;U32 context=0x20000100,source=0x21000080,requested=16,offset=3,consumed=0,total=64;
    U32 corrupt=0,protect=0,return_pc=stop;unsigned permission=Memory::Read,slice=4096,repeats=1;
    std::uint64_t budget=10000;
};
struct Result {Cpu cpu;std::uint64_t budget{},instructions{},batches{};std::string fault;std::vector<std::uint8_t> bytes;double seconds{};};
Result execute(const Image& image,const Case& c,bool fast){
    Memory m;const auto mapped=image.mapped();
    for(const auto [at,size]:regions){
        m.map(at,size,Memory::Read|Memory::Write|(at==0x49f000?Memory::Execute:0));
        std::vector<std::uint8_t> data(size);for(unsigned n=0;n<size;++n)data[n]=std::uint8_t(n*37+(n>>4)+at/4096);
        if(at==0x49f000 || at==0x4d2000)std::copy_n(mapped.data()+at-image.base,size,data.data());
        m.initialize(at,data);
    }
    const auto sp=c.cpu.r[ESP];
    // Invalid setup pointers are left unmapped for the actual function to fault.
    try{m.store(sp,stop,32);m.store(sp+4,c.context,32);m.store(sp+8,c.requested,32);}catch(const GuestFault&){}
    try{m.store(c.context,c.consumed,32);m.store(c.context+4,c.offset,32);m.store(c.context+12,c.source,32);m.store(c.context+16,c.total,32);}catch(const GuestFault&){}
    if(c.corrupt){const auto byte=m.load(c.corrupt,8);m.initialize(c.corrupt,std::array<std::uint8_t,1>{std::uint8_t(byte^255)});}
    if(c.protect)m.protect(c.protect,4096,c.permission);
    auto cpu=c.cpu;auto budget=c.budget;Result result;const auto started=std::chrono::steady_clock::now();
    try{
        for(unsigned repeat=0;repeat<c.repeats;++repeat){
            cpu=c.cpu;
            while(cpu.eip>=begin && cpu.eip<end && cpu.eip!=c.return_pc){
                auto scope=m.execution_scope();unsigned count=0;
                while(count<c.slice && cpu.eip>=begin && cpu.eip<end && cpu.eip!=c.return_pc){
                    if(fast)if(const auto n=wa2_bitreader_batch(cpu,m,budget,c.slice-count,c.return_pc)){CHECK(n<=c.slice-count);count+=n;result.instructions+=n;++result.batches;continue;}
                    tick(budget,cpu.eip);CHECK(code_page_1183(cpu,m,budget));++count;++result.instructions;
                }
            }
        }
    }catch(const GuestFault& fault){result.fault=std::to_string(int(fault.kind))+":"+std::to_string(fault.address);}
    result.seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
    result.cpu=cpu;result.budget=budget;
    for(const auto [at,size]:regions){m.protect(at,size,Memory::Read);const auto offset=result.bytes.size();result.bytes.resize(offset+size);m.copy_out(at,std::span(result.bytes).subspan(offset,size));}
    return result;
}
void equal(const Result& a,const Result& b,unsigned number){
    if(a.cpu.r!=b.cpu.r || a.cpu.eip!=b.cpu.eip || a.cpu.flags!=b.cpu.flags || a.cpu.defined_flags!=b.cpu.defined_flags || a.instructions!=b.instructions || a.budget!=b.budget || a.fault!=b.fault || a.bytes!=b.bytes){
        std::cerr<<"case "<<number<<" PC "<<std::hex<<a.cpu.eip<<'/'<<b.cpu.eip<<std::dec<<" instructions "<<a.instructions<<'/'<<b.instructions<<" fault "<<a.fault<<'/'<<b.fault<<'\n';
        throw std::runtime_error("bitreader differs from generated oracle");
    }
    CHECK(a.cpu.fp.bytes==b.cpu.fp.bytes && a.cpu.selectors==b.cpu.selectors && a.cpu.fs_base==b.cpu.fs_base && a.cpu.gs_base==b.cpu.gs_base && a.cpu.fs_valid==b.cpu.fs_valid && a.cpu.gs_valid==b.cpu.gs_valid);
}
Case make_case(std::mt19937& random){
    Case c;for(auto& r:c.cpu.r)r=random();for(auto& b:c.cpu.fp.bytes)b=std::uint8_t(random());
    c.cpu.eip=begin;c.cpu.r[ESP]=0x07000100;c.cpu.flags=random();c.cpu.defined_flags=random();
    c.cpu.fs_base=random();c.cpu.gs_base=random();c.cpu.fs_valid=true;c.cpu.gs_valid=true;return c;
}
}
int main(int argc,char** argv){try{
    CHECK(argc==3);const auto image=Image::load(argv[1]);std::mt19937 random(0x49fb20);unsigned cases=0;std::uint64_t batches=0;
    const auto verify=[&](const Case& c){const auto a=execute(image,c,false),b=execute(image,c,true);equal(a,b,cases++);batches+=b.batches;};
    for(unsigned request=0;request<=32;++request)for(unsigned offset=0;offset<8;++offset)for(unsigned tail=0;tail<5;++tail){auto c=make_case(random);c.requested=request;c.offset=offset;c.total=10;c.consumed=6+tail;verify(c);}
    for(unsigned n=0;n<500;++n){auto c=make_case(random);c.requested=random()%33;c.offset=random()%8;c.total=random();c.consumed=random();c.slice=1+random()%80;c.budget=random()%100;verify(c);}
    for(unsigned cut=0;cut<75;++cut){auto c=make_case(random);c.budget=cut;verify(c);c.budget=10000;c.slice=cut+1;verify(c);}
    for(const auto request:{33u,255u,0xffffffffu}){auto c=make_case(random);c.requested=request;c.budget=100;verify(c);}
    for(const auto offset:{8u,31u,0xffffffffu}){auto c=make_case(random);c.offset=offset;c.budget=100;verify(c);}
    for(const auto pc:{begin,0x49fb28u,0x49fb55u,0x49fb56u,0x49fbc0u,end}){auto c=make_case(random);c.return_pc=pc;verify(c);}
    for(const auto at:{begin,begin+100,end-1}){auto c=make_case(random);c.corrupt=at;verify(c);}
    for(const auto page:{0x07000000u,0x20000000u,0x21000000u,0x4d2000u,0x49f000u})for(const auto permission:{unsigned(Memory::Read),unsigned(Memory::Write)}){auto c=make_case(random);c.protect=page;c.permission=permission;verify(c);}
    for(const auto context:{0u,0xfffffff0u,0x070000f0u,0x49fb30u,0x20001ff8u}){auto c=make_case(random);c.context=context;c.budget=100;verify(c);}
    for(const auto source:{0u,0xfffffffcu,0x21001ffeu,0x070000f0u,0x49fb30u}){auto c=make_case(random);c.source=source;c.requested=32;verify(c);}
    for(const auto sp:{0u,8u,0xfffffffcU,0x07001ffcu,0x20000104u,0x49fb40u}){auto c=make_case(random);c.cpu.r[ESP]=sp;c.budget=100;verify(c);}
    CHECK(batches>0);auto c=make_case(random);c.repeats=100000;c.requested=32;c.offset=7;c.budget=100000000;
    const auto before=execute(image,c,false),after=execute(image,c,true);equal(before,after,cases++);CHECK(after.batches==c.repeats && after.fault.empty());
    std::ofstream report(argv[2]);report<<"{\"schema\":\"wa2.bitreader-batch-tests.v1\",\"cases\":"<<cases<<",\"exact_cpu_flags_fpu_memory_budget_and_faults\":true,\"guest_instructions\":"<<after.instructions<<",\"before_seconds\":"<<before.seconds<<",\"after_seconds\":"<<after.seconds<<",\"speedup\":"<<before.seconds/after.seconds<<",\"memory_sha256\":\""<<sha256(after.bytes)<<"\"}\n";CHECK(report);
    std::cout<<"PASS "<<cases<<" exact bitreader cases; "<<before.seconds<<" -> "<<after.seconds<<" seconds ("<<before.seconds/after.seconds<<"x)\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
