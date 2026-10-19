// Differential oracle: the unmodified generated instruction bodies execute the
// same checked-memory cases as the fixed native MDCT batch.
#include "vorbis_batch.hpp"
#include <winrecomp/compiled.hpp>
#include <winrecomp/core.hpp>
#include <winrecomp/hash.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <random>

bool code_page_1134(wr::Cpu&,wr::Memory&,std::uint64_t&);
using namespace wr;
#define CHECK(x) do { if(!(x))throw std::runtime_error(#x); } while(false)
namespace {
constexpr U32 begin=0x46ea23,end=0x46eb14,output=0x20000000,table=0x21000000;
struct Case {
    Cpu cpu;
    std::vector<std::uint8_t> data,weights;
    std::uint64_t budget=100000;
    unsigned slice=4096;
    U32 return_pc=0xffffffc0,corrupt=0;
    bool readonly=false;
};
struct Result {
    Cpu cpu;
    std::vector<std::uint8_t> data,weights;
    std::uint64_t budget{},instructions{},batches{};
    std::string fault;
    double seconds{};
};
Result execute(std::span<const std::uint8_t> code,const Case& c,bool fast) {
    Memory m;
    m.map(0x46e000,4096,Memory::Read|Memory::Execute);m.initialize(0x46e000,code);
    m.map(output,c.data.size(),Memory::Read|Memory::Write);m.copy_in(output,c.data);
    m.map(table,c.weights.size(),Memory::Read|Memory::Write);m.copy_in(table,c.weights);
    if(c.corrupt)m.initialize(c.corrupt,std::array<std::uint8_t,1>{0xcc});
    if(c.readonly)m.protect(output,c.data.size(),Memory::Read);
    auto cpu=c.cpu;auto budget=c.budget;Result result;
    const auto started=std::chrono::steady_clock::now();
    try {
        while(cpu.eip>=begin && cpu.eip<end) {
            auto guard=m.execution_scope();unsigned count=0;
            while(count<c.slice && cpu.eip>=begin && cpu.eip<end) {
                if(fast)if(auto used=wa2_vorbis_batch(cpu,m,budget,c.slice-count,c.return_pc)) {
                    CHECK(used==92 && used<=c.slice-count);
                    count+=used;result.instructions+=used;++result.batches;continue;
                }
                tick(budget,cpu.eip);CHECK(code_page_1134(cpu,m,budget));
                ++count;++result.instructions;
            }
        }
    }catch(const GuestFault& fault) {
        result.fault=std::to_string(int(fault.kind))+":"+std::to_string(fault.address);
    }
    result.seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
    result.cpu=cpu;result.budget=budget;
    result.data.resize(c.data.size());result.weights.resize(c.weights.size());
    m.copy_out(output,result.data);m.copy_out(table,result.weights);
    return result;
}
void equal(const Result& a,const Result& b,unsigned case_number) {
    if(a.cpu.fp.bytes!=b.cpu.fp.bytes) {
        for(unsigned k=0;k<512;++k)if(a.cpu.fp.bytes[k]!=b.cpu.fp.bytes[k]) {
            std::cerr<<"case "<<case_number<<" FP byte "<<k<<" expected="<<unsigned(a.cpu.fp.bytes[k])
                <<" actual="<<unsigned(b.cpu.fp.bytes[k])<<" CW="<<a.cpu.fp.get16(0)<<'\n';break;
        }
    }
    CHECK(a.cpu.r==b.cpu.r && a.cpu.eip==b.cpu.eip && a.cpu.flags==b.cpu.flags && a.cpu.defined_flags==b.cpu.defined_flags);
    CHECK(a.cpu.fp.bytes==b.cpu.fp.bytes && a.cpu.selectors==b.cpu.selectors);
    CHECK(a.cpu.fs_base==b.cpu.fs_base && a.cpu.gs_base==b.cpu.gs_base && a.cpu.fs_valid==b.cpu.fs_valid && a.cpu.gs_valid==b.cpu.gs_valid);
    CHECK(a.budget==b.budget && a.instructions==b.instructions && a.data==b.data && a.weights==b.weights && a.fault==b.fault);
}
Case make_case(std::mt19937& random,unsigned iterations,unsigned mode) {
    Case c;c.data.resize(32768);c.weights.resize(32768);
    // Ordinary audio magnitudes plus signed zeros, subnormals, infinities,
    // quiet/signaling NaNs and overflow/underflow inputs. No C++ FP conversion.
    static constexpr U32 specials[]={0,0x80000000,1,0x80000001,0x007fffff,0x00800000,
        0x7f800000,0xff800000,0x7fc12345,0x7f812345,0x7f7fffff,0xff7fffff};
    for(auto* values:{&c.data,&c.weights})for(unsigned k=0;k<values->size();k+=4) {
        U32 value=(random()&0x807fffffu)|((118+random()%16)<<23);
        if(mode && random()%4==0)value=specials[random()%std::size(specials)];
        for(unsigned n=0;n<4;++n)(*values)[k+n]=std::uint8_t(value>>(n*8));
    }
    for(auto& r:c.cpu.r)r=random();
    c.cpu.eip=begin;c.cpu.r[EAX]=output+0x100+(iterations-1)*32;
    c.cpu.r[ECX]=output+0x1000+0x1c+(iterations-1)*32;c.cpu.r[EDI]=output+0x100;
    c.cpu.r[EDX]=table+0x100;c.cpu.r[ESI]=(random()%9)*4;
    c.cpu.flags=random();c.cpu.defined_flags=random();
    const unsigned precision[]={0,2,3};
    c.cpu.fp.put16(0,std::uint16_t(0x7f|(precision[random()%3]<<8)|((random()%4)<<10)));
    const auto top=random()%8,live=mode?random()%9:random()%5;
    c.cpu.fp.put16(2,std::uint16_t((top<<11)|(random()&0x473f)));
    for(unsigned k=0;k<live;++k)c.cpu.fp.bytes[4]|=std::uint8_t(1u<<((top+k)&7));
    for(unsigned k=0;k<8;++k) {
        c.cpu.fp.put64(32+k*16,(std::uint64_t(random())<<32)|random()|(1ull<<63));
        c.cpu.fp.put16(40+k*16,std::uint16_t(0x3ff0+random()%30));
        for(unsigned n=10;n<16;++n)c.cpu.fp.bytes[32+k*16+n]=std::uint8_t(random());
    }
    c.cpu.fp.put64(8,0x46e900);c.cpu.fp.put64(16,output);c.cpu.fp.put16(6,0x123);
    // This includes the eight Win64 nonvolatile XMM registers. The batch must
    // preserve every byte of all guest vector registers and reserved storage.
    for(unsigned k=160;k<512;++k)c.cpu.fp.bytes[k]=std::uint8_t(random());
    return c;
}
}
int main(int argc,char** argv) {try {
    CHECK(argc==3);const auto image=Image::load(argv[1]);
    const auto code=image.code(0x46e000).first(4096);
    std::mt19937 random(0x46ea23);unsigned cases=0;std::uint64_t batches=0;
    for(unsigned n=0;n<800;++n) {
        auto c=make_case(random,1+random()%12,n%2);
        c.slice=1+random()%800;
        if(n%3==0)c.budget=random()%1000;
        if(n%17==0)c.corrupt=begin+random()%(end-begin);
        if(n%19==0)c.readonly=true;
        if(n%23==0)c.return_pc=begin+random()%(end-begin);
        if(n%29==0)c.cpu.fp.put16(0,std::uint16_t(c.cpu.fp.get16(0)&~(1u<<(random()%6))));
        const auto before=execute(code,c,false),after=execute(code,c,true);
        equal(before,after,cases++);batches+=after.batches;
    }
    // Every budget cut through the 92-instruction body must preserve the fault
    // instruction, exact partially updated memory and the extended FP state.
    for(unsigned cut=0;cut<185;++cut) {
        auto c=make_case(random,3,cut%2);c.budget=cut;
        equal(execute(code,c,false),execute(code,c,true),cases++);
    }
    for(unsigned kind=0;kind<9;++kind) {
        auto c=make_case(random,1,1);c.budget=300;
        if(kind==0)c.cpu.r[ECX]=c.cpu.r[EAX]+28;
        if(kind==1)c.cpu.r[ECX]=c.cpu.r[EAX]+40;
        if(kind==2)c.cpu.r[EDX]=c.cpu.r[EAX]+4;
        if(kind==3)c.cpu.r[EDX]=c.cpu.r[ECX]-4;
        if(kind==4)c.cpu.r[EAX]=output+U32(c.data.size())-16;
        if(kind==5)c.cpu.r[ECX]=output+8;
        if(kind==6)c.cpu.r[EDX]=0xfffffffc;
        if(kind==7)c.cpu.r[ESI]=0xfffffffc;
        if(kind==8)c.cpu.r[EAX]=begin;
        equal(execute(code,c,false),execute(code,c,true),cases++);
    }
    CHECK(batches>0);
    for(const auto control:{0x003fu,0x00ffu,0xff3fu,0xffffu,0x3f3fu,0x557fu,0xaa7fu,0x027fu}) {
        auto c=make_case(random,3,1);c.cpu.fp.put16(0,std::uint16_t(control));
        equal(execute(code,c,false),execute(code,c,true),cases++);
    }
    // Stable workload benchmark: all arithmetic and output data still match.
    auto benchmark=make_case(random,250,0);benchmark.cpu.r[ESI]=8;
    const auto before=execute(code,benchmark,false),after=execute(code,benchmark,true);
    equal(before,after,cases++);CHECK(after.batches>=240 && after.instructions==250*92);
    std::ofstream report(argv[2]);
    report<<"{\"schema\":\"wa2.vorbis-batch-tests.v1\",\"cases\":"<<cases
        <<",\"exact_cpu_flags_fpu_memory_budget_and_faults\":true,\"guest_instructions\":"<<after.instructions
        <<",\"before_seconds\":"<<before.seconds<<",\"after_seconds\":"<<after.seconds
        <<",\"speedup\":"<<before.seconds/after.seconds<<",\"batch_count\":"<<after.batches
        <<",\"memory_sha256\":\""<<sha256(after.data)<<"\"}\n";
    CHECK(report);
    std::cout<<"PASS "<<cases<<" exact Vorbis batch cases; "<<before.seconds<<" -> "<<after.seconds<<" seconds ("<<before.seconds/after.seconds<<"x)\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
