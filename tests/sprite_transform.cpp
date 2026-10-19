// Whole-block differential oracle: exact x87, integer flags, scratch stores,
// branch cost, slice boundaries, signatures and partial-fault fallback.
#include "sprite_transform.hpp"
#include <winrecomp/compiled.hpp>
#include <winrecomp/core.hpp>
#include <winrecomp/hash.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <random>
bool code_page_1083(wr::Cpu&,wr::Memory&,std::uint64_t&);
using namespace wr;
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(false)
namespace {
constexpr U32 begin=0x43bba8,end=0x43bde0,memory_base=0x07000000,palette_base=0x4c2000;
struct Case {Cpu cpu;std::vector<std::uint8_t> data,palette;U32 corrupt{},protect{},return_pc{0xffffffc0};unsigned permissions{Memory::Read},slice{4096},repeats{1};std::uint64_t budget{100000};bool writable_code{},audit{true};};
struct Result {Cpu cpu;std::vector<std::uint8_t> data,palette,code;std::uint64_t budget{},instructions{},batches{};std::string fault;double seconds{};};
bool same_cpu(const Cpu& a,const Cpu& b){return a.r==b.r && a.eip==b.eip && a.flags==b.flags && a.defined_flags==b.defined_flags && a.fp.bytes==b.fp.bytes && a.selectors==b.selectors && a.fs_base==b.fs_base && a.gs_base==b.gs_base && a.fs_valid==b.fs_valid && a.gs_valid==b.gs_valid;}
Result execute(std::span<const std::uint8_t> code,const Case& c,bool fast){
    Memory m;m.map(0x43b000,4096,Memory::Read|Memory::Execute|(c.writable_code?Memory::Write:0));m.initialize(0x43b000,code);
    m.map(memory_base,c.data.size(),Memory::Read|Memory::Write);m.copy_in(memory_base,c.data);
    m.map(palette_base,c.palette.size(),Memory::Read|Memory::Write);m.copy_in(palette_base,c.palette);
    if(c.corrupt)m.initialize(c.corrupt,std::array<std::uint8_t,1>{0xcc});if(c.protect)m.protect(c.protect,4096,c.permissions);
    Cpu cpu=c.cpu;auto budget=c.budget;Result result;const auto start=std::chrono::steady_clock::now();
    try{
        for(unsigned repetition=0;repetition<c.repeats;++repetition){
            if(repetition){cpu=c.cpu;for(auto offset:{0x10u,0x18u}){std::array<std::uint8_t,4> word{};std::copy_n(c.data.data()+c.cpu.r[ESP]-memory_base+offset,4,word.begin());m.copy_in(cpu.r[ESP]+offset,word);}}
            const auto running=[&]{return cpu.eip>=begin && cpu.eip<end && cpu.eip!=c.return_pc;};
            while(running()){
                auto scope=m.execution_scope();unsigned count=0;
                while(count<c.slice && running()){
                    if(fast){
                        const auto old_cpu=cpu;const auto old_budget=budget;
                        const auto n=wa2_sprite_transform_batch(cpu,m,budget,c.slice-count,c.return_pc);
                        if(n){CHECK(n<=c.slice-count);count+=n;result.instructions+=n;++result.batches;continue;}
                        if(c.audit)CHECK(same_cpu(old_cpu,cpu) && old_budget==budget);
                    }
                    tick(budget,cpu.eip);CHECK(code_page_1083(cpu,m,budget));++count;++result.instructions;
                }
            }
        }
    }catch(const GuestFault& f){result.fault=std::to_string(int(f.kind))+":"+std::to_string(f.address);}
    result.seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();result.cpu=cpu;result.budget=budget;
    m.protect(memory_base,c.data.size(),Memory::Read);m.protect(palette_base,c.palette.size(),Memory::Read);
    result.data.resize(c.data.size());m.copy_out(memory_base,result.data);result.palette.resize(c.palette.size());m.copy_out(palette_base,result.palette);result.code.resize(code.size());m.copy_out(0x43b000,result.code);return result;
}
void equal(const Result& a,const Result& b,unsigned number){
    if(a.cpu.fp.bytes!=b.cpu.fp.bytes)for(unsigned i=0;i<512;++i)if(a.cpu.fp.bytes[i]!=b.cpu.fp.bytes[i]){std::cerr<<"case "<<number<<" FPbyte "<<i<<'='<<unsigned(a.cpu.fp.bytes[i])<<'/'<<unsigned(b.cpu.fp.bytes[i])<<'\n';break;}
    if(a.cpu.r!=b.cpu.r)for(unsigned i=0;i<8;++i)if(a.cpu.r[i]!=b.cpu.r[i])std::cerr<<"register "<<i<<'='<<std::hex<<a.cpu.r[i]<<'/'<<b.cpu.r[i]<<std::dec<<'\n';
    if(a.cpu.flags!=b.cpu.flags)std::cerr<<"flags="<<std::hex<<a.cpu.flags<<'/'<<b.cpu.flags<<std::dec<<'\n';
    if(a.data!=b.data)for(unsigned i=0;i<a.data.size();++i)if(a.data[i]!=b.data[i]){std::cerr<<"memorybyte "<<i<<'='<<unsigned(a.data[i])<<'/'<<unsigned(b.data[i])<<'\n';break;}
    CHECK(same_cpu(a.cpu,b.cpu));CHECK(a.data==b.data && a.palette==b.palette && a.code==b.code && a.budget==b.budget && a.instructions==b.instructions && a.fault==b.fault);
}
void put(Case& c,U32 address,U32 value){CHECK(address>=memory_base && std::uint64_t(address)+4<=std::uint64_t(memory_base)+c.data.size());for(unsigned i=0;i<4;++i)c.data[address-memory_base+i]=std::uint8_t(value>>(8*i));}
Case make_case(std::mt19937& random,bool special){
    Case c;c.data.resize(0x4000);c.palette.resize(0x1000);for(auto& byte:c.data)byte=std::uint8_t(random());for(auto& byte:c.palette)byte=std::uint8_t(random());
    for(auto& r:c.cpu.r)r=random();c.cpu.eip=begin;c.cpu.r[ESP]=memory_base+0x100+(random()%4);c.cpu.r[ESI]=memory_base+0x2000+(random()%4);
    c.cpu.flags=random();c.cpu.defined_flags=random();c.cpu.fs_base=random();c.cpu.gs_base=random();c.cpu.fs_valid=true;c.cpu.gs_valid=true;for(auto& selector:c.cpu.selectors)selector=std::uint16_t(random());
    static constexpr U32 specials[]={0,0x80000000,1,0x80000001,0x007fffff,0x00800000,0x7f800000,0xff800000,0x7fc12345,0x7f812345,0x7f7fffff,0xff7fffff};
    const auto value=[&]{return special?specials[random()%std::size(specials)]:(random()&0x807fffffu)|((120+random()%20)<<23);};
    for(auto offset:{0x78u,0x7cu,0x80u,0x84u,0x88u,0x8cu,0x90u,0x94u,0x98u,0x9cu,0xa0u,0xa4u,0xbcu,0xc4u})put(c,c.cpu.r[ESI]+offset,value());
    put(c,c.cpu.r[ESP]+0x10,value());put(c,c.cpu.r[ESP]+0x18,value());put(c,c.cpu.r[ESI]+0x44,random());
    const unsigned precision[]={0,2,3};c.cpu.fp.put16(0,std::uint16_t(0x7f|(precision[random()%3]<<8)|((random()%4)<<10)));
    const unsigned top=random()%8,live=special?random()%9:0;c.cpu.fp.put16(2,std::uint16_t((top<<11)|(random()&0x473f)));
    for(unsigned i=0;i<live;++i)c.cpu.fp.bytes[4]|=std::uint8_t(1u<<((top+i)&7));
    for(unsigned i=0;i<8;++i){c.cpu.fp.put64(32+i*16,(std::uint64_t(random())<<32)|random()|(1ull<<63));c.cpu.fp.put16(40+i*16,std::uint16_t(0x3ff0+random()%30));for(unsigned j=10;j<16;++j)c.cpu.fp.bytes[32+i*16+j]=std::uint8_t(random());}
    c.cpu.fp.put64(8,0x43bb97);c.cpu.fp.put64(16,memory_base);c.cpu.fp.put16(6,0x123);c.cpu.fp.put32(24,random()&0xffff);for(unsigned i=160;i<512;++i)c.cpu.fp.bytes[i]=std::uint8_t(random());return c;
}
}
int main(int argc,char** argv){try{
    CHECK(argc==3);const auto image=Image::load(argv[1]);const auto code=image.code(0x43b000).first(4096);std::mt19937 random(0x43bba8);unsigned cases=0;std::uint64_t batches=0;
    const auto verify=[&](const Case& c){const auto a=execute(code,c,false),b=execute(code,c,true);try{equal(a,b,cases);}catch(...){std::cerr<<"Transform case="<<cases<<" slice="<<c.slice<<" budget="<<c.budget<<" instructions="<<a.instructions<<'/'<<b.instructions<<" fault="<<a.fault<<'/'<<b.fault<<'\n';throw;}++cases;batches+=b.batches;};
    for(unsigned n=0;n<1600;++n){auto c=make_case(random,n%2);c.slice=1+random()%500;if(n%3==0)c.budget=random()%200;if(n%17==0)c.corrupt=begin+random()%(end-begin);if(n%19==0)c.protect=memory_base;if(n%23==0)c.return_pc=begin+random()%(end-begin);if(n%29==0)c.cpu.fp.put16(0,std::uint16_t(c.cpu.fp.get16(0)&~(1u<<(random()%6))));verify(c);}
    for(unsigned cut=0;cut<=170;++cut){auto c=make_case(random,cut%2);c.budget=cut;verify(c);c.budget=10000;c.slice=cut+1;verify(c);}
    for(auto cw:{0x003fu,0x007fu,0x03ffu,0x803fu,0xffffu}){auto c=make_case(random,true);c.cpu.fp.put16(0,std::uint16_t(cw));verify(c);}
    for(unsigned flip=0;flip<4;++flip)for(auto x:{0xbf800000u,0u,0x3f800000u,0x7fc12345u})for(auto y:{0xbf800000u,0u,0x3f800000u,0x7fc12345u}){auto c=make_case(random,false);put(c,c.cpu.r[ESI]+0x44,flip);put(c,c.cpu.r[ESI]+0xbc,x);put(c,c.cpu.r[ESI]+0xc4,y);verify(c);}
    for(unsigned index=0;index<256;++index){auto c=make_case(random,false);c.data[c.cpu.r[ESI]-memory_base+0x52]=std::uint8_t(index);verify(c);}
    for(auto address:{memory_base,memory_base+0x2000,palette_base})for(auto permissions:{unsigned(Memory::Read),unsigned(Memory::Write),0u}){auto c=make_case(random,true);c.protect=address;c.permissions=permissions;verify(c);}
    for(auto sp:{0u,0xffffff80u,memory_base+0x3f80,memory_base+0x2000,palette_base+0x220,begin}){auto c=make_case(random,true);c.cpu.r[ESP]=sp;c.writable_code=true;verify(c);}
    for(auto sprite:{0u,0xffffff80u,memory_base+0x3f80,memory_base+0x100,palette_base+0x220,begin}){auto c=make_case(random,true);c.cpu.r[ESI]=sprite;verify(c);}
    for(auto pc:{begin,0x43bbbfu,0x43bbf3u,0x43bc40u,0x43bd7du,0x43bdd7u,end}){auto c=make_case(random,true);c.return_pc=pc;verify(c);}
    CHECK(batches>0);auto c=make_case(random,false);c.repeats=100000;c.budget=100000000;c.audit=false;
    const auto before=execute(code,c,false),after=execute(code,c,true);equal(before,after,cases++);CHECK(after.fault.empty() && after.batches==c.repeats);
    std::ofstream report(argv[2]);report<<"{\"schema\":\"wa2.sprite-transform-tests.v1\",\"cases\":"<<cases<<",\"exact_cpu_flags_fpu_memory_budget_and_faults\":true,\"transforms\":"<<c.repeats<<",\"guest_instructions\":"<<after.instructions<<",\"before_seconds\":"<<before.seconds<<",\"after_seconds\":"<<after.seconds<<",\"speedup\":"<<before.seconds/after.seconds<<",\"batches\":"<<after.batches<<",\"memory_sha256\":\""<<sha256(after.data)<<"\"}\n";CHECK(report);
    std::cout<<"PASS "<<cases<<" sprite transform equivalence cases; "<<before.seconds<<" -> "<<after.seconds<<" seconds ("<<before.seconds/after.seconds<<"x)\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
