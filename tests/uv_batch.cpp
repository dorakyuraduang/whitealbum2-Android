// Exact differential oracle against both original generated UV loops, including
// the second loop's out-of-line upper-clamp branch at 0x43d77d.
#include "uv_batch.hpp"
#include <winrecomp/compiled.hpp>
#include <winrecomp/core.hpp>
#include <winrecomp/hash.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <random>
bool code_page_1084(wr::Cpu&,wr::Memory&,std::uint64_t&);
bool code_page_1085(wr::Cpu&,wr::Memory&,std::uint64_t&);
using namespace wr;
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(false)
namespace {
constexpr U32 memory_base=0x07000000;
struct Case {Cpu cpu;std::vector<std::uint8_t> data;bool y{},readonly{};U32 corrupt{},return_pc{0xffffffc0};unsigned slice{4096},repeats{1};std::uint64_t budget{100000};};
struct Result {Cpu cpu;std::vector<std::uint8_t> data,code;std::uint64_t budget{},instructions{},batches{};std::string fault;double seconds{};};
Result execute(std::span<const std::uint8_t> code,const Case& c,bool fast){
    const U32 begin=c.y?0x43cd37:0x43cc4d,end=c.y?0x43cdf0:0x43cd06;
    Memory m;m.map(0x43c000,8192,Memory::Read|Memory::Execute);m.initialize(0x43c000,code);m.map(memory_base,c.data.size(),Memory::Read|Memory::Write);m.copy_in(memory_base,c.data);
    if(c.corrupt)m.initialize(c.corrupt,std::array<std::uint8_t,1>{0xcc});if(c.readonly)m.protect(memory_base,c.data.size(),Memory::Read);
    Cpu cpu=c.cpu;auto budget=c.budget;Result result;const auto start=std::chrono::steady_clock::now();
    try{
        for(unsigned repetition=0;repetition<c.repeats;++repetition){
            if(repetition){cpu=c.cpu;m.store(cpu.r[ESP]+0x30,0,32);}
            const auto running=[&]{return cpu.eip!=c.return_pc && ((cpu.eip>=begin&&cpu.eip<end)||(c.y&&cpu.eip>=0x43d77d&&cpu.eip<0x43d78c));};
            while(running()){
                auto scope=m.execution_scope();unsigned count=0;
                while(count<c.slice && running()){
                    if(fast)if(auto n=wa2_uv_batch(cpu,m,budget,c.slice-count,c.return_pc)){CHECK(n<=c.slice-count);count+=n;result.instructions+=n;++result.batches;continue;}
                    tick(budget,cpu.eip);CHECK(cpu.eip<0x43d000?code_page_1084(cpu,m,budget):code_page_1085(cpu,m,budget));++count;++result.instructions;
                }
            }
        }
    }catch(const GuestFault& f){result.fault=std::to_string(int(f.kind))+":"+std::to_string(f.address);}
    result.seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();result.cpu=cpu;result.budget=budget;
    result.data.resize(c.data.size());m.copy_out(memory_base,result.data);result.code.resize(code.size());m.copy_out(0x43c000,result.code);return result;
}
void equal(const Result& a,const Result& b,unsigned number){
    if(a.cpu.fp.bytes!=b.cpu.fp.bytes)for(unsigned i=0;i<512;++i)if(a.cpu.fp.bytes[i]!=b.cpu.fp.bytes[i]){std::cerr<<"case "<<number<<" FPbyte "<<i<<'='<<unsigned(a.cpu.fp.bytes[i])<<'/'<<unsigned(b.cpu.fp.bytes[i])<<'\n';break;}
    if(a.cpu.r!=b.cpu.r)for(unsigned i=0;i<8;++i)if(a.cpu.r[i]!=b.cpu.r[i])std::cerr<<"register "<<i<<'='<<std::hex<<a.cpu.r[i]<<'/'<<b.cpu.r[i]<<std::dec<<'\n';
    if(a.instructions!=b.instructions)std::cerr<<"instructions="<<a.instructions<<'/'<<b.instructions<<'\n';
    CHECK(a.cpu.r==b.cpu.r && a.cpu.eip==b.cpu.eip && a.cpu.flags==b.cpu.flags && a.cpu.defined_flags==b.cpu.defined_flags);
    CHECK(a.cpu.fp.bytes==b.cpu.fp.bytes && a.cpu.selectors==b.cpu.selectors && a.cpu.fs_base==b.cpu.fs_base && a.cpu.gs_base==b.cpu.gs_base && a.cpu.fs_valid==b.cpu.fs_valid && a.cpu.gs_valid==b.cpu.gs_valid);
    CHECK(a.data==b.data && a.code==b.code && a.budget==b.budget && a.instructions==b.instructions && a.fault==b.fault);
}
Case make_case(std::mt19937& random,bool y,unsigned iterations,unsigned mode){
    Case c;c.y=y;c.data.resize(8192);for(auto& byte:c.data)byte=std::uint8_t(random());for(auto& r:c.cpu.r)r=random();c.cpu.eip=y?0x43cd37:0x43cc4d;c.cpu.r[ESP]=memory_base+0x100;c.cpu.r[ECX]=0;c.cpu.r[y?ESI:EBX]=iterations;c.cpu.r[EDX]=random()%512;
    c.cpu.flags=random();c.cpu.defined_flags=random();c.cpu.fs_base=random();c.cpu.gs_base=random();c.cpu.fs_valid=true;c.cpu.gs_valid=true;
    const auto put=[&](unsigned offset,U32 value){for(unsigned i=0;i<4;++i)c.data[0x100+offset+i]=std::uint8_t(value>>(8*i));};put(0x30,0);
    static constexpr U32 specials[]={0,0x80000000,1,0x80000001,0x007fffff,0x00800000,0x7f800000,0xff800000,0x7fc12345,0x7f812345,0x7f7fffff,0xff7fffff};
    for(auto offset:{0x68u,0x6cu})put(offset,mode?specials[random()%std::size(specials)]:(random()&0x807fffffu)|((120+random()%20)<<23));
    const unsigned precision[]={0,2,3};c.cpu.fp.put16(0,std::uint16_t(0x7f|(precision[random()%3]<<8)|((random()%4)<<10)));
    const auto top=random()%8,live=mode?random()%9:5;c.cpu.fp.put16(2,std::uint16_t((top<<11)|(random()&0x473f)));
    for(unsigned i=0;i<live;++i)c.cpu.fp.bytes[4]|=std::uint8_t(1u<<((top+i)&7));
    for(unsigned i=0;i<8;++i){c.cpu.fp.put64(32+i*16,(std::uint64_t(random())<<32)|random()|(1ull<<63));c.cpu.fp.put16(40+i*16,std::uint16_t(0x3ff0+random()%30));for(unsigned j=10;j<16;++j)c.cpu.fp.bytes[32+i*16+j]=std::uint8_t(random());}
    // Common finite UV state: ST0=1, ST1=0, ST2=512, ST3=0, ST4=512.
    if(!mode){const std::uint64_t sig[]={1ull<<63,0,1ull<<63,0,1ull<<63};const std::uint16_t exp[]={0x3fff,0,0x4008,0,0x4008};for(unsigned i=0;i<5;++i){c.cpu.fp.put64(32+i*16,sig[i]);c.cpu.fp.put16(40+i*16,exp[i]);}}
    c.cpu.fp.put64(8,0x43cc00);c.cpu.fp.put64(16,memory_base);c.cpu.fp.put16(6,0x123);for(unsigned i=160;i<512;++i)c.cpu.fp.bytes[i]=std::uint8_t(random());return c;
}
}
int main(int argc,char** argv){try{
    CHECK(argc==3);const auto image=Image::load(argv[1]);const auto code=image.code(0x43c000).first(8192);std::mt19937 random(0x43cc4d);unsigned cases=0;std::uint64_t batches=0;
    const auto verify=[&](const Case& c){const auto a=execute(code,c,false),b=execute(code,c,true);try{equal(a,b,cases);}catch(...){std::cerr<<"UV axis="<<c.y<<" case="<<cases<<" slice="<<c.slice<<" budget="<<c.budget<<" fault="<<a.fault<<'/'<<b.fault<<'\n';throw;}++cases;batches+=b.batches;};
    for(unsigned axis=0;axis<2;++axis){
        for(unsigned n=0;n<800;++n){auto c=make_case(random,axis!=0,1+random()%20,n%2);c.slice=1+random()%400;if(n%3==0)c.budget=random()%1000;if(n%17==0)c.corrupt=c.cpu.eip+random()%185;if(axis&&n%37==0)c.corrupt=0x43d77d+random()%15;if(n%19==0)c.readonly=true;if(n%23==0)c.return_pc=c.cpu.eip+random()%185;if(n%29==0)c.cpu.fp.put16(0,std::uint16_t(c.cpu.fp.get16(0)&~(1u<<(random()%6))));verify(c);}
        for(unsigned cut=0;cut<160;++cut){auto c=make_case(random,axis!=0,3,cut%2);c.budget=cut;verify(c);}
        for(auto cw:{0x003fu,0x007fu,0x03ffu,0x803fu,0xffffu}){auto c=make_case(random,axis!=0,3,1);c.cpu.fp.put16(0,std::uint16_t(cw));verify(c);}
        for(unsigned kind=0;kind<10;++kind){auto c=make_case(random,axis!=0,2,1);c.budget=300;if(kind==0)c.cpu.r[ESP]=0xfffffff0;if(kind==1)c.cpu.r[ESP]=memory_base+8100;if(kind==2)c.cpu.r[ECX]=0xffffffff;if(kind==3)c.cpu.r[ECX]=0x40000000;if(kind==4)c.cpu.r[ECX]=1700;if(kind==5)c.cpu.r[EDX]=0xffffffff;if(kind==6)c.cpu.r[axis?ESI:EBX]=0x80000000;if(kind==7)c.return_pc=0x43d781;if(kind==8)c.cpu.r[ESP]=0x43cc30;if(kind==9)c.cpu.fp.put16(0,0x37e);verify(c);}
    }
    CHECK(batches>0);std::array<Result,2> before,after;
    for(unsigned axis=0;axis<2;++axis){auto c=make_case(random,axis!=0,512,0);c.repeats=100;c.budget=100000000;c.cpu.r[EDX]=1;before[axis]=execute(code,c,false);after[axis]=execute(code,c,true);equal(before[axis],after[axis],cases++);CHECK(after[axis].fault.empty()&&after[axis].batches>50000);}
    std::ofstream report(argv[2]);report<<"{\"schema\":\"wa2.uv-batch-tests.v1\",\"cases\":"<<cases<<",\"exact_cpu_flags_fpu_memory_budget_and_faults\":true,\"benchmarks\":[";
    for(unsigned axis=0;axis<2;++axis){if(axis)report<<',';report<<"{\"axis\":"<<axis<<",\"guest_instructions\":"<<after[axis].instructions<<",\"before_seconds\":"<<before[axis].seconds<<",\"after_seconds\":"<<after[axis].seconds<<",\"speedup\":"<<before[axis].seconds/after[axis].seconds<<",\"batches\":"<<after[axis].batches<<",\"memory_sha256\":\""<<sha256(after[axis].data)<<"\"}";}report<<"]}\n";CHECK(report);
    std::cout<<"PASS "<<cases<<" UV equivalence cases; X "<<before[0].seconds<<" -> "<<after[0].seconds<<" seconds ("<<before[0].seconds/after[0].seconds<<"x); Y "<<before[1].seconds<<" -> "<<after[1].seconds<<" seconds ("<<before[1].seconds/after[1].seconds<<"x)\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
