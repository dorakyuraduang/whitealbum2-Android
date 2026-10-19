// Independent differential oracle: the original per-bit implementation is
// preserved below, rather than checking the closed form against itself.
#include <winrecomp/integer.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <random>
using namespace wr;
namespace {
U32 reference_shift(Cpu& s,U32 a,U32 count,unsigned w,unsigned kind) {
    a&=mask(w);const U32 sign=U32(1)<<(w-1);const unsigned masked=count&31u;
    if(!masked)return a;
    unsigned n=masked;U32 carry=s.flags&CF;
    if(kind>=5 && w<32)n%=w+1;
    if(!n) {s.defined_flags&=~OF;return a;}
    const U32 old=a;
    for(unsigned k=0;k<n;++k) {
        if(kind==0 || kind==3 || kind==5){const U32 next=(a&sign)?1u:0u;a=((a<<1)|(kind==3?next:kind==5?carry:0u))&mask(w);carry=next;}
        else {const U32 next=a&1u;a=(a>>1)|(kind==2?(a&sign):kind==4?(next?sign:0u):kind==6?(carry?sign:0u):0u);carry=next;}
    }
    if(kind<3) {
        s.flags=(s.flags&~(SF|ZF|PF|CF))|szp(a,w)|(carry?CF:0u);
        s.defined_flags=(s.defined_flags|SF|ZF|PF|CF)&~AF;
        if(kind<2 && masked>=w)s.defined_flags&=~CF;
    } else {s.flags=(s.flags&~CF)|(carry?CF:0u);s.defined_flags|=CF;}
    if(masked==1) {
        U32 of=0;
        if(kind==0 || kind==3 || kind==5)of=bool(a&sign)!=bool(carry)?OF:0u;
        else if(kind==1)of=(old&sign)?OF:0u;
        else if(kind==4 || kind==6)of=bool(a&sign)!=bool(a&(sign>>1))?OF:0u;
        s.flags=(s.flags&~OF)|of;s.defined_flags|=OF;
    }else s.defined_flags&=~OF;
    return a;
}
std::uint64_t cases=0;
Cpu expected,actual;
void check(U32 value,U32 count,unsigned width,unsigned kind,U32 flags,U32 defined){
    expected.flags=actual.flags=flags;expected.defined_flags=actual.defined_flags=defined;
    const auto a=reference_shift(expected,value,count,width,kind),b=shift(actual,value,count,width,kind);
    if(a!=b || expected.flags!=actual.flags || expected.defined_flags!=actual.defined_flags){
        std::cerr<<"value="<<std::hex<<value<<" count="<<count<<std::dec<<" width="<<width<<" kind="<<kind<<" result="<<a<<"/"<<b<<" flags="<<expected.flags<<"/"<<actual.flags<<" defined="<<expected.defined_flags<<"/"<<actual.defined_flags<<'\n';
        throw std::runtime_error("closed-form shift differs from the original bit loop");
    }
    ++cases;
}
struct BenchResult {double seconds{};U32 checksum{},flags{},defined{};};
template<bool fast> BenchResult benchmark(unsigned width,unsigned kind){
    Cpu cpu;U32 value=0x9abcdef1,checksum=0,count=1;constexpr unsigned repeats=3000000;
    const auto start=std::chrono::steady_clock::now();
    for(unsigned n=0;n<repeats;++n){
        count=count*1664525u+1013904223u;
        if constexpr(fast)value=shift(cpu,value^count,count>>27,width,kind);
        else value=reference_shift(cpu,value^count,count>>27,width,kind);
        checksum+=value^cpu.flags^cpu.defined_flags;
    }
    return {std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count(),checksum,cpu.flags,cpu.defined_flags};
}
}
int main(int argc,char** argv){try{
    if(argc!=2)throw std::runtime_error("usage: shift_fast_tests report.json");
    std::mt19937 random(0x51f700);
    for(unsigned n=0;n<8;++n)expected.r[n]=actual.r[n]=random();
    for(unsigned n=0;n<512;++n)expected.fp.bytes[n]=actual.fp.bytes[n]=std::uint8_t(random());
    expected.eip=actual.eip=0x43b290;expected.fs_base=actual.fs_base=random();expected.gs_base=actual.gs_base=random();
    // Every 8-bit input and count byte, both input carry values and both OFs.
    for(unsigned value=0;value<256;++value)for(unsigned count=0;count<256;++count)for(unsigned kind=0;kind<7;++kind)for(unsigned state=0;state<4;++state)
        check(value,count,8,kind,(state&1?CF:0)|(state&2?OF:0)|0x60236u,(value*0x9e3779b9u)^count);
    // Every 16-bit input, every masked count, and both initial carry values.
    // Also pass high input bits, since the operation must truncate its input.
    for(unsigned value=0;value<65536;++value)for(unsigned count=0;count<32;++count)for(unsigned kind=0;kind<7;++kind)for(unsigned carry=0;carry<2;++carry)
        check(value|0xabcd0000u,count,16,kind,((value*0x9e3779b9u)&~CF)|carry,(value*17u)^count);
    const U32 edges[]={0,1,2,0x7f,0x80,0xff,0x100,0x7fff,0x8000,0xffff,0x10000,0x7fffffff,0x80000000,0xfffffffe,0xffffffff,0x55555555,0xaaaaaaaa};
    for(auto value:edges)for(unsigned count=0;count<256;++count)for(unsigned kind=0;kind<7;++kind)for(unsigned carry=0;carry<2;++carry)
        check(value,0xffffff00u|count,32,kind,(random()&~CF)|carry,random());
    for(unsigned n=0;n<1000000;++n)check(random(),random(),std::array<unsigned,3>{8,16,32}[n%3],n%7,random(),random());
    if(expected.r!=actual.r || expected.fp.bytes!=actual.fp.bytes || expected.eip!=actual.eip || expected.fs_base!=actual.fs_base || expected.gs_base!=actual.gs_base)
        throw std::runtime_error("shift modified unrelated CPU state");
    std::ofstream report(argv[1]);report<<"{\"schema\":\"wa2.shift-closed-form-tests.v1\",\"cases\":"<<cases<<",\"exact_values_flags_and_defined_flags\":true,\"exhaustive_8_and_16_bit_values\":true,\"benchmarks\":[";
    bool first=true;
    for(unsigned width:{8u,16u,32u})for(unsigned kind=0;kind<7;++kind){
        const auto before=benchmark<false>(width,kind),after=benchmark<true>(width,kind);
        if(before.checksum!=after.checksum || before.flags!=after.flags || before.defined!=after.defined)throw std::runtime_error("benchmark state mismatch");
        if(!first)report<<',';first=false;
        report<<"{\"width\":"<<width<<",\"kind\":"<<kind<<",\"before_seconds\":"<<before.seconds<<",\"after_seconds\":"<<after.seconds<<",\"speedup\":"<<before.seconds/after.seconds<<'}';
        std::cout<<width<<"-bit kind "<<kind<<": "<<before.seconds/after.seconds<<"x\n";
    }
    report<<"]}\n";if(!report)throw std::runtime_error("report write failed");
    std::cout<<"PASS "<<cases<<" exact shift/rotate cases\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
