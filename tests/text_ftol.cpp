#include "../recompiled/process.hpp"
#include "../recompiled/text_ftol.hpp"
#include "../recompiled/text_ftol_bridge.hpp"
#include "../recompiled/extended80.hpp"
#include "../generated/shared-aot/bodies.hpp"
#include <winrecomp/win32_state.hpp>
#include <array>
#include <bit>
#include <cerrno>
#include <cfenv>
#include <cstring>
#include <fstream>
#include <iostream>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("ftol check at ")+std::to_string(__LINE__)+": "+#x);}while(false)
namespace a=wa2::aot;
bool same(const wr::Cpu& x,const wr::Cpu& y){return x.r==y.r && x.eip==y.eip && x.flags==y.flags && x.defined_flags==y.defined_flags && x.fp.bytes==y.fp.bytes;}
int main(int argc,char** argv){try{
    CHECK(argc==2);a::Runtime runtime;runtime.load();auto& p=runtime.process();auto& m=p.memory;const auto data=p.allocate_bytes(16384);
    m.store(0xbc8310,1,32);unsigned cases{},fractional_cases{},bridge_cases{},declines{},environments{};
    auto initial=p.cpu;for(unsigned n=0;n<8;++n)initial.r[n]=0x12340000u+n*0x101;
    const auto setup=[&](a::fp::Extended value,unsigned alignment,unsigned top,unsigned cw,unsigned mxcsr){
        auto c=initial;c.r[wr::ESP]=data+4096+128+alignment;c.eip=0x474f00;c.flags=0xa97;c.defined_flags=0x89;
        c.fp.put16(0,std::uint16_t(cw));c.fp.put16(2,std::uint16_t((top<<11)|0x4725));c.fp.put32(24,mxcsr);c.fp.put32(28,0x12);
        c.fp.bytes[4]=std::uint8_t((0xa5u|(1u<<top))&255);
        for(unsigned n=0;n<8;++n){c.fp.put64(32+n*16,0x8000000000000000ull+n*0x100);c.fp.put16(40+n*16,0x4000);
            std::fill_n(c.fp.bytes.data()+42+n*16,6,std::uint8_t(0x31+n));}
        c.fp.put64(32,value.significand);c.fp.put16(40,value.sign_exponent);return c;
    };
    const std::array<std::int64_t,14> values{0,1,-1,2,-2,28,-28,255,-255,16384,-16384,2147483647,-2147483647,-2147483648ll};
    std::array<std::uint8_t,256> stack;for(unsigned n=0;n<stack.size();++n)stack[n]=std::uint8_t(n*37);
    for(auto number:values)for(unsigned align=0;align<8;++align)for(unsigned top=0;top<8;++top)for(unsigned cw:{0x7fu,0x27fu,0xf7fu})for(unsigned mxcsr:{0x1f80u,0xffa5u}){
        auto value=a::fp::from_integer(number);if(!number && (mxcsr&0x8000))value.sign_exponent=0x8000;
        auto c=setup(value,align,top,cw,mxcsr);const auto start=c.r[wr::ESP]-64;
        const auto reset=[&]{m.copy_in(start,stack);m.store(c.r[wr::ESP],0xf0001234,32);};
        reset();auto expected=c;a::enable_integer_ftol(false);{auto scope=m.execution_scope();std::uint64_t unused{};a::f_401260<false>(expected,m,scope,unused,92);}
        std::array<std::uint8_t,256> bytes;m.copy_out(start,bytes);
        reset();auto actual=c;a::enable_integer_ftol(true);{auto scope=m.execution_scope();CHECK(a::native_integer_ftol(actual,m,scope));}
        std::array<std::uint8_t,256> after;m.copy_out(start,after);CHECK(same(actual,expected) && after==bytes);++cases;
    }
    const auto compare_fraction=[&](a::fp::Extended value,unsigned align,unsigned top,unsigned cw,unsigned mxcsr){
        auto c=setup(value,align,top,cw,mxcsr);const auto start=c.r[wr::ESP]-64;
        const auto reset=[&]{m.copy_in(start,stack);m.store(c.r[wr::ESP],0xf0001234,32);};
        reset();auto expected=c;a::enable_integer_ftol(false);{auto scope=m.execution_scope();std::uint64_t unused{};a::f_401260<false>(expected,m,scope,unused,92);}
        std::array<std::uint8_t,256> bytes;m.copy_out(start,bytes);
        reset();auto actual=c;a::enable_integer_ftol(true);{auto scope=m.execution_scope();CHECK(a::native_integer_ftol(actual,m,scope));}
        std::array<std::uint8_t,256> after;m.copy_out(start,after);CHECK(same(actual,expected) && after==bytes);++fractional_cases;
    };
    const std::array<a::fp::Extended,16> fractions{{
        {0x8000000000000000ull,0x3ffe},{0x8000000000000000ull,0xbffe},
        {0xa000000000000000ull,0x3fff},{0xa000000000000000ull,0xbfff},
        {0xe400000000000000ull,0x4003},{0xe400000000000000ull,0xc003},
        {0xccccccccccccd000ull,0x3ffb},{0xccccccccccccd000ull,0xbffb},
        {0xfffffffffffff800ull,0x3ffe},{0xfffffffffffff800ull,0xbffe},
        {0xffffffff00000000ull,0x401d},{0xffffffff00000000ull,0xc01d},
        {0x8000000080000000ull,0xc01e},{0x80000000fffff800ull,0xc01e},
        {0x8000000000000000ull,0x3c01},{0x8000000000000000ull,0xbc01}
    }};
    for(auto value:fractions)for(unsigned align=0;align<8;++align)for(unsigned top=0;top<8;++top)
        for(unsigned cw:{0x7fu,0x27fu,0x37fu,0xf7fu})for(unsigned mxcsr:{0x1f80u,0xffa5u,0x9fc5u})compare_fraction(value,align,top,cw,mxcsr);
    std::uint32_t random=0x49713;
    for(unsigned n=0;n<512;++n){random=random*1664525u+1013904223u;
        const a::fp::Extended value{std::uint64_t(0x800000u|(random&0x7fffffu))<<40,std::uint16_t(n&1?0xbffe:0x3ffe)};
        compare_fraction(value,n&7,(n>>3)&7,0x7f,n&1?0xffa5:0x1f80);
    }
    const auto compare_bridge=[&](a::fp::Extended value,unsigned align,unsigned top,unsigned cw,unsigned mxcsr,bool active){
        auto c=setup(value,align,top,cw,mxcsr);const auto start=c.r[wr::ESP]-64;
        const auto reset=[&]{m.copy_in(start,stack);m.store(c.r[wr::ESP],0xf0001234,32);};
        reset();auto expected=c;a::enable_integer_ftol(false);
        if(active){expected.eip=0x700010;a::fp::x87_execute<a::fp::x87<217,200,false>>(expected,nullptr,456,false,false);expected.eip=0x474f00;}
        {auto scope=m.execution_scope();std::uint64_t unused{};a::f_401260<false>(expected,m,scope,unused,92);}
        std::array<std::uint8_t,256> bytes;m.copy_out(start,bytes);
        reset();auto actual=c;a::enable_integer_ftol(true);
        {auto scope=m.execution_scope();a::fp::RegisterFrame<false> frame(actual,m,scope);const auto before_ip=actual.fp.get64(8);
            if(active){actual.eip=0x700010;frame.execute<217,200,false>(nullptr,456,false,false);actual.eip=0x474f00;}
            CHECK(a::fp::TextFtolAdapter::invoke(actual,m,scope,frame));
            if(active)CHECK(actual.fp.get64(8)==before_ip);
            frame.flush();}
        std::array<std::uint8_t,256> after;m.copy_out(start,after);CHECK(same(actual,expected) && after==bytes);++bridge_cases;
    };
    for(bool active:{false,true}){
        for(auto number:values)for(unsigned align=0;align<8;++align)for(unsigned top=0;top<8;++top)for(unsigned cw:{0x7fu,0x27fu,0xf7fu})for(unsigned mxcsr:{0x1f80u,0xffa5u})
            compare_bridge(a::fp::from_integer(number),align,top,cw,mxcsr,active);
        for(auto value:fractions)for(unsigned align=0;align<8;++align)for(unsigned top=0;top<8;++top)for(unsigned cw:{0x7fu,0x27fu,0x37fu,0xf7fu})for(unsigned mxcsr:{0x1f80u,0xffa5u,0x9fc5u})
            compare_bridge(value,align,top,cw,mxcsr,active);
    }
    const auto reject=[&](wr::Cpu c){const auto before=c;auto scope=m.execution_scope();CHECK(!a::native_integer_ftol(c,m,scope) && same(c,before));++declines;};
    auto ordinary=setup(a::fp::from_integer(28),0,0,0x7f,0x1f80);m.store(ordinary.r[wr::ESP],0xf0001234,32);
    for(auto v:{a::fp::Extended{0x8000000000000001ull,0x3fff},a::fp::Extended{0xa000000000000400ull,0x3fff},a::fp::Extended{0x8000000000000000ull,0x401e},a::fp::Extended{0x8000000000000000ull,0x401f},a::fp::Extended{0xc000000000000000ull,0x7fff},a::fp::Extended{0x8000000000000000ull,0x7fff},a::fp::Extended{1,0}})reject(setup(v,0,0,0x7f,0x1f80));
    for(unsigned variant=0;variant<7;++variant){auto c=ordinary;
        switch(variant){case 0:c.fp.bytes[4]=0;break;case 1:c.fp.put16(0,0x7e);break;case 2:c.fp.put16(2,0x80);break;case 3:c.fp.put32(24,0x1f00);break;case 4:c.eip=0x474f07;break;case 5:c.r[wr::ESP]=4;break;case 6:c.r[wr::ESP]=0xeffff123;break;}reject(c);}
    m.store(0xbc8310,0,32);reject(ordinary);m.store(0xbc8310,1,32);a::enable_integer_ftol(false);reject(ordinary);a::enable_integer_ftol(true);
    // Host error/FP state is not part of the source integer conversion.
    for(bool fractional:{false,true})for(int raised:{0,FE_INVALID,FE_DIVBYZERO,FE_INEXACT}){std::fenv_t saved;std::fegetenv(&saved);std::feclearexcept(FE_ALL_EXCEPT);if(raised)std::feraiseexcept(raised);errno=EDOM;
        auto c=fractional?setup(fractions[2],0,0,0x7f,0x1f80):ordinary;auto scope=m.execution_scope();CHECK(a::native_integer_ftol(c,m,scope));CHECK(errno==EDOM && std::fetestexcept(FE_ALL_EXCEPT)==raised);std::fesetenv(&saved);++environments;}
    std::ofstream out(argv[1]);out<<"{\"exact_cpu_fp_stack_cases\":"<<cases<<",\"exact_fractional_cases\":"<<fractional_cases<<",\"register_bridge_cases\":"<<bridge_cases<<",\"bridge_keeps_live_registers\":true,\"declines\":"<<declines<<",\"host_environment_cases\":"<<environments<<",\"reference_is_cached_source\":true,\"inexact_binary64_inputs_keep_source\":true,\"fractional_mxcsr_and_x87_state_checked\":true,\"game_launched\":false,\"screen_access\":false,\"source_executable_opened\":false,\"passed\":true}\n";CHECK(out);
    std::cout<<"PASS exact integer FTOL versus cached body, full FP/stack and decline guards.\n";
}catch(const std::exception& error){a::enable_integer_ftol(true);std::cerr<<error.what()<<'\n';return 1;}}
