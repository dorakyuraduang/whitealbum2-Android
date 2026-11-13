#include "../recompiled/text_spacing.hpp"
#include "../generated/shared-aot/bodies.hpp"
#include <array>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("spacing check at ")+std::to_string(__LINE__)+": "+#x);}while(false)
namespace a=wa2::aot;
namespace wa2::aot::text_spacing_reference {void invoke(wr::Cpu&,wr::Memory&,Scope&);}
bool same(const wr::Cpu& x,const wr::Cpu& y){return x.r==y.r && x.eip==y.eip && x.flags==y.flags && x.defined_flags==y.defined_flags && x.fp.bytes==y.fp.bytes;}
wr::Cpu initial(std::uint32_t number,unsigned alignment,unsigned seed){
    wr::Cpu c;for(unsigned n=0;n<8;++n)c.r[n]=0x38120300u+seed+n*0x314159u;
    c.r[wr::EAX]=number;c.r[wr::ESP]=0x101800+alignment;c.eip=0x445350;c.flags=seed^0xa597;c.defined_flags=seed^0x9fc2;
    for(unsigned n=0;n<c.fp.bytes.size();++n)c.fp.bytes[n]=std::uint8_t(n*13+seed);
    return c;
}
int main(int argc,char** argv){try{
    CHECK(argc==2);wr::Memory m;m.map(0x100000,0x3000,3);m.map(0xb34000,4096,3);unsigned cases{},guards{};
    std::array<std::uint8_t,64> pattern;for(unsigned n=0;n<pattern.size();++n)pattern[n]=std::uint8_t(n*37);
    const auto compare=[&](std::uint32_t number,std::uint32_t carry,unsigned alignment,unsigned seed){
        const auto input=initial(number,alignment,seed);const auto start=input.r[wr::ESP]-32;
        const auto reset=[&]{m.copy_in(start,pattern);m.store(input.r[wr::ESP],0xf0001234,32);m.store(0xb34544,carry,32);};
        reset();auto expected=input;{auto scope=m.execution_scope();a::text_spacing_reference::invoke(expected,m,scope);}
        std::array<std::uint8_t,64> expected_bytes;m.copy_out(start,expected_bytes);const auto expected_carry=m.load(0xb34544,32);
        reset();auto actual=input;{auto scope=m.execution_scope();CHECK(a::native_text_spacing(actual,m,scope));}
        std::array<std::uint8_t,64> after;m.copy_out(start,after);
        CHECK(same(actual,expected) && after==expected_bytes && m.load(0xb34544,32)==expected_carry);++cases;
    };
    const std::array<std::uint32_t,20> numbers{0,1,2,9,10,11,19,255,1001,0x7fffffffu,0x7ffffffeu,0x80000000u,0x80000001u,0xffffffffu,0xfffffffeu,0xfffffff7u,0xfffffff6u,0xfffffff5u,0xffffffedu,0xfffffc17u};
    const std::array<std::uint32_t,12> carries{0,1,9,10,255,0x7fffffffu,0x80000000u,0xffffffffu,0xfffffffeu,0xfffffff7u,0xfffffff6u,0xfffffc17u};
    for(auto number:numbers)for(auto carry:carries)for(unsigned align=0;align<8;++align)compare(number,carry,align,number^carry^align);
    std::uint32_t random=0x12348acd;
    for(unsigned n=0;n<4096;++n){random=random*1664525u+1013904223u;const auto number=random;random=random*1664525u+1013904223u;compare(number,random,n&7,n*0x1234567u);}
    struct Result {wr::Cpu cpu;std::optional<std::pair<wr::FaultKind,wr::U32>> fault;std::vector<std::uint8_t> memory;std::uint64_t epoch{};};
    const auto guarded=[&](unsigned variant,bool native){
        wr::Memory memory;memory.map(0x100000,0x3000,3);memory.map(0xb34000,4096,3);memory.map(0xfffff000,4096,3);
        Result result;result.cpu=initial(123,0,0x4713);auto& c=result.cpu;
        switch(variant){case 0:c.r[wr::ESP]=3;break;case 1:c.r[wr::ESP]=0x101002;break;case 2:c.r[wr::ESP]=0xb34548;break;case 3:c.r[wr::ESP]=0xb3454c;break;case 4:c.r[wr::ESP]=0xfffffffdu;break;default:break;}
        memory.store(0xb34544,9,32);
        if(variant!=0 && variant!=4)memory.store(c.r[wr::ESP],0xf0001234,32);
        if(variant==5)memory.protect(0x101000,4096,wr::Memory::Read);
        if(variant==6)memory.protect(0x101000,4096,wr::Memory::Write);
        if(variant==7)memory.protect(0x101000,4096,7);
        if(variant==8)memory.protect(0xb34000,4096,wr::Memory::Read);
        if(variant==9){c.r[wr::EAX]=0xffffffffu;memory.protect(0xb34000,4096,wr::Memory::Write);}
        if(variant==10)memory.protect(0xb34000,4096,7);
        if(variant==11)memory.decommit(0xb34000,4096);
        if(native){const auto before=c;auto scope=memory.execution_scope();CHECK(!a::native_text_spacing(c,memory,scope) && same(c,before));}
        try{auto scope=memory.execution_scope();if(native){std::uint64_t unused{};a::f_445350<false>(c,memory,scope,unused,0);}else a::text_spacing_reference::invoke(c,memory,scope);}
        catch(const wr::GuestFault& f){result.fault={{f.kind,f.address}};}
        result.epoch=memory.code_epoch();
        memory.protect(0x100000,0x3000,3);memory.protect(0xfffff000,4096,3);
        if(variant!=11)memory.protect(0xb34000,4096,3);
        result.memory.resize(0x4000+(variant==11?0:0x1000));memory.copy_out(0x100000,{result.memory.data(),0x3000});memory.copy_out(0xfffff000,{result.memory.data()+0x3000,0x1000});
        if(variant!=11)memory.copy_out(0xb34000,{result.memory.data()+0x4000,0x1000});
        return result;
    };
    for(unsigned n=0;n<12;++n){const auto expected=guarded(n,false),actual=guarded(n,true);CHECK(same(expected.cpu,actual.cpu) && expected.fault==actual.fault && expected.memory==actual.memory && expected.epoch==actual.epoch);++guards;}
    std::ofstream out(argv[1]);out<<"{\"cases\":"<<cases<<",\"fallback_guards\":"<<guards<<",\"full_cpu_flags_fp_stack_and_carry_equal\":true,\"reference_is_cached_source\":true,\"overflow_and_sentinels_checked\":true,\"game_launched\":false,\"screen_access\":false,\"source_executable_opened\":false,\"passed\":true}\n";CHECK(out);
    std::cout<<"PASS native text spacing against cached instruction body.\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
