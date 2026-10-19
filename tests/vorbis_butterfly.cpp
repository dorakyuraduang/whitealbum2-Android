// Differential validation against every original generated instruction body.
#include "vorbis_butterfly.hpp"
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
constexpr U32 begin=0x46e477,end=0x46e95c,data_at=0x20000000,stack_at=0x07000000,constants=0x4a6e8c;
struct Case {
    Cpu cpu;
    std::vector<std::uint8_t> data,stack;
    std::array<std::uint8_t,12> constants{};
    std::uint64_t budget=10000;
    unsigned slice=4096;
    U32 return_pc=0xffffffc0,corrupt=0;
    bool readonly_data=false,readonly_stack=false,inaccessible_constants=false;
};
struct Result {
    Cpu cpu;
    std::vector<std::uint8_t> data,stack;
    std::uint64_t budget{},instructions{},batches{};
    std::string fault;
    double seconds{};
};
Result execute(std::span<const std::uint8_t> code,const Case& c,bool fast) {
    Memory m;
    m.map(0x46e000,4096,Memory::Read|Memory::Execute);m.initialize(0x46e000,code);
    m.map(data_at,c.data.size(),Memory::Read|Memory::Write);m.copy_in(data_at,c.data);
    m.map(stack_at,c.stack.size(),Memory::Read|Memory::Write);m.copy_in(stack_at,c.stack);
    m.map(0x4a6000,4096,Memory::Read);m.initialize(constants,c.constants);
    if(c.corrupt)m.initialize(c.corrupt,std::array<std::uint8_t,1>{0xcc});
    if(c.readonly_data)m.protect(data_at,c.data.size(),Memory::Read);
    if(c.readonly_stack)m.protect(stack_at,c.stack.size(),Memory::Read);
    if(c.inaccessible_constants)m.protect(0x4a6000,4096,0);
    auto cpu=c.cpu;auto budget=c.budget;Result result;
    const auto started=std::chrono::steady_clock::now();
    try {
        while(cpu.eip>=begin && cpu.eip<end) {
            auto guard=m.execution_scope();unsigned count=0;
            while(count<c.slice && cpu.eip>=begin && cpu.eip<end) {
                if(fast)if(auto used=wa2_vorbis_butterfly(cpu,m,budget,c.slice-count,c.return_pc)) {
                    CHECK(used==404 && used<=c.slice-count);
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
    result.data.resize(c.data.size());result.stack.resize(c.stack.size());
    m.copy_out(data_at,result.data);m.copy_out(stack_at,result.stack);
    return result;
}
void equal(const Result& a,const Result& b,unsigned number) {
    if(a.cpu.fp.bytes!=b.cpu.fp.bytes)for(unsigned k=0;k<512;++k)if(a.cpu.fp.bytes[k]!=b.cpu.fp.bytes[k]) {
        std::cerr<<"case "<<number<<" FP byte "<<k<<" expected="<<unsigned(a.cpu.fp.bytes[k])
            <<" actual="<<unsigned(b.cpu.fp.bytes[k])<<" CW="<<a.cpu.fp.get16(0)<<'\n';break;
    }
    if(a.cpu.r!=b.cpu.r)for(unsigned k=0;k<8;++k)if(a.cpu.r[k]!=b.cpu.r[k])
        std::cerr<<"case "<<number<<" register "<<k<<" expected="<<a.cpu.r[k]<<" actual="<<b.cpu.r[k]<<'\n';
    CHECK(a.cpu.r==b.cpu.r && a.cpu.eip==b.cpu.eip && a.cpu.flags==b.cpu.flags && a.cpu.defined_flags==b.cpu.defined_flags);
    CHECK(a.cpu.fp.bytes==b.cpu.fp.bytes && a.cpu.selectors==b.cpu.selectors);
    CHECK(a.cpu.fs_base==b.cpu.fs_base && a.cpu.gs_base==b.cpu.gs_base && a.cpu.fs_valid==b.cpu.fs_valid && a.cpu.gs_valid==b.cpu.gs_valid);
    CHECK(a.budget==b.budget && a.instructions==b.instructions && a.data==b.data && a.stack==b.stack && a.fault==b.fault);
}
Case make_case(std::mt19937& random,bool special) {
    Case c;c.data.resize(4096);c.stack.resize(4096);
    static constexpr U32 specials[]={0,0x80000000,1,0x80000001,0x007fffff,0x00800000,
        0x7f800000,0xff800000,0x7fc12345,0x7f812345,0x7f7fffff,0xff7fffff};
    auto fill=[&](std::span<std::uint8_t> bytes) {
        for(unsigned k=0;k<bytes.size();k+=4) {
            U32 value=(random()&0x807fffffu)|((118+random()%16)<<23);
            if(special && random()%4==0)value=specials[random()%std::size(specials)];
            for(unsigned n=0;n<4;++n)bytes[k+n]=std::uint8_t(value>>(n*8));
        }
    };
    fill(c.data);fill(c.stack);fill(c.constants);
    for(auto& r:c.cpu.r)r=random();
    c.cpu.eip=begin;c.cpu.r[EAX]=data_at+128+(random()%1600);c.cpu.r[ESP]=stack_at+128+(random()%1600);
    c.cpu.flags=random();c.cpu.defined_flags=random();
    const unsigned precision[]={0,2,3};
    c.cpu.fp.put16(0,std::uint16_t(0x7f|(precision[random()%3]<<8)|((random()%4)<<10)));
    const auto top=random()%8,live=special?random()%9:random()%5;
    c.cpu.fp.put16(2,std::uint16_t((top<<11)|(random()&0x473f)));
    for(unsigned k=0;k<live;++k)c.cpu.fp.bytes[4]|=std::uint8_t(1u<<((top+k)&7));
    for(unsigned k=0;k<8;++k) {
        c.cpu.fp.put64(32+k*16,(std::uint64_t(random())<<32)|random()|(1ull<<63));
        c.cpu.fp.put16(40+k*16,std::uint16_t(0x3ff0+random()%30));
        for(unsigned n=10;n<16;++n)c.cpu.fp.bytes[32+k*16+n]=std::uint8_t(random());
    }
    c.cpu.fp.put64(8,0x46e400);c.cpu.fp.put64(16,data_at);c.cpu.fp.put16(6,0x123);
    for(unsigned k=160;k<512;++k)c.cpu.fp.bytes[k]=std::uint8_t(random());
    return c;
}
}
int main(int argc,char** argv) {try {
    CHECK(argc==3);const auto image=Image::load(argv[1]);const auto code=image.code(0x46e000).first(4096);
    std::mt19937 random(0x46e477);unsigned cases=0;std::uint64_t batches=0;
    for(unsigned n=0;n<600;++n) {
        auto c=make_case(random,n%2);c.slice=1+random()%850;
        if(n%3==0)c.budget=random()%500;
        if(n%17==0)c.corrupt=begin+random()%(end-begin);
        if(n%19==0)c.readonly_data=true;
        if(n%23==0)c.readonly_stack=true;
        if(n%29==0)c.return_pc=begin+random()%(end-begin);
        if(n%31==0)c.inaccessible_constants=true;
        if(n%37==0)c.cpu.fp.put16(0,std::uint16_t(c.cpu.fp.get16(0)&~(1u<<(random()%6))));
        const auto before=execute(code,c,false),after=execute(code,c,true);
        equal(before,after,cases++);batches+=after.batches;
    }
    for(unsigned cut=0;cut<=404;++cut) {
        auto c=make_case(random,cut%2);c.budget=cut;
        equal(execute(code,c,false),execute(code,c,true),cases++);
    }
    for(unsigned kind=0;kind<12;++kind) {
        auto c=make_case(random,true);
        if(kind==0)c.cpu.r[ESP]=c.cpu.r[EAX]+64;
        if(kind==1)c.cpu.r[EAX]=c.cpu.r[ESP]-16;
        if(kind==2)c.cpu.r[EAX]=constants;
        if(kind==3)c.cpu.r[ESP]=constants+4;
        if(kind==4)c.cpu.r[EAX]=data_at+4096-64;
        if(kind==5)c.cpu.r[ESP]=stack_at+4096-8;
        if(kind==6)c.cpu.r[EAX]=0xffffff80;
        if(kind==7)c.cpu.r[ESP]=0xfffffff4;
        if(kind==8)c.cpu.r[ESP]=2;
        if(kind==9)c.cpu.r[EAX]=begin;
        if(kind==10)c.cpu.r[ESP]=begin+4;
        if(kind==11)c.cpu.r[EAX]=c.cpu.r[ESP]+8;
        equal(execute(code,c,false),execute(code,c,true),cases++);
    }
    CHECK(batches>0);
    for(const auto control:{0x003fu,0x00ffu,0xff3fu,0xffffu,0x3f3fu,0x557fu,0xaa7fu,0x027fu}) {
        auto c=make_case(random,true);c.cpu.fp.put16(0,std::uint16_t(control));
        equal(execute(code,c,false),execute(code,c,true),cases++);
    }
    double before_seconds=0,after_seconds=0;std::vector<std::uint8_t> final_data;
    for(unsigned n=0;n<200;++n) {
        auto c=make_case(random,false);c.cpu.r[EAX]=data_at+128;c.cpu.r[ESP]=stack_at+128;
        const auto before=execute(code,c,false),after=execute(code,c,true);
        equal(before,after,cases++);CHECK(after.batches==1 && after.instructions==404);
        before_seconds+=before.seconds;after_seconds+=after.seconds;final_data=after.data;
    }
    std::ofstream report(argv[2]);
    report<<"{\"schema\":\"wa2.vorbis-butterfly-tests.v1\",\"cases\":"<<cases
        <<",\"exact_cpu_flags_fpu_memory_budget_and_faults\":true,\"guest_instructions\":80800"
        <<",\"before_seconds\":"<<before_seconds<<",\"after_seconds\":"<<after_seconds
        <<",\"speedup\":"<<before_seconds/after_seconds<<",\"batch_count\":200"
        <<",\"memory_sha256\":\""<<sha256(final_data)<<"\"}\n";
    CHECK(report);
    std::cout<<"PASS "<<cases<<" exact butterfly cases; "<<before_seconds<<" -> "<<after_seconds<<" seconds ("<<before_seconds/after_seconds<<"x)\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
