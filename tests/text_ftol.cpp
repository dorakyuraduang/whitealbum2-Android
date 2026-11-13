#include "../recompiled/process.hpp"
#include "../recompiled/text_ftol.hpp"
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
    m.store(0xbc8310,1,32);unsigned cases{},declines{},environments{};
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
    const auto reject=[&](wr::Cpu c){const auto before=c;auto scope=m.execution_scope();CHECK(!a::native_integer_ftol(c,m,scope) && same(c,before));++declines;};
    auto ordinary=setup(a::fp::from_integer(28),0,0,0x7f,0x1f80);m.store(ordinary.r[wr::ESP],0xf0001234,32);
    for(auto v:{a::fp::Extended{0x8000000000000000ull,0x3ffe},a::fp::Extended{0xa000000000000000ull,0x3fff},a::fp::Extended{0x8000000000000000ull,0x401e},a::fp::Extended{0x8000000000000000ull,0x401f},a::fp::Extended{0xc000000000000000ull,0x7fff},a::fp::Extended{0x8000000000000000ull,0x7fff},a::fp::Extended{1,0}})reject(setup(v,0,0,0x7f,0x1f80));
    for(unsigned variant=0;variant<7;++variant){auto c=ordinary;
        switch(variant){case 0:c.fp.bytes[4]=0;break;case 1:c.fp.put16(0,0x7e);break;case 2:c.fp.put16(2,0x80);break;case 3:c.fp.put32(24,0x1f00);break;case 4:c.eip=0x474f07;break;case 5:c.r[wr::ESP]=4;break;case 6:c.r[wr::ESP]=0xeffff123;break;}reject(c);}
    m.store(0xbc8310,0,32);reject(ordinary);m.store(0xbc8310,1,32);a::enable_integer_ftol(false);reject(ordinary);a::enable_integer_ftol(true);
    // Host error/FP state is not part of the source integer conversion.
    for(int raised:{0,FE_INVALID,FE_DIVBYZERO,FE_INEXACT}){std::fenv_t saved;std::fegetenv(&saved);std::feclearexcept(FE_ALL_EXCEPT);if(raised)std::feraiseexcept(raised);errno=EDOM;
        auto c=ordinary;auto scope=m.execution_scope();CHECK(a::native_integer_ftol(c,m,scope));CHECK(errno==EDOM && std::fetestexcept(FE_ALL_EXCEPT)==raised);std::fesetenv(&saved);++environments;}
    std::ofstream out(argv[1]);out<<"{\"exact_cpu_fp_stack_cases\":"<<cases<<",\"declines\":"<<declines<<",\"host_environment_cases\":"<<environments<<",\"reference_is_cached_source\":true,\"noninteger_inputs_keep_source\":true,\"game_launched\":false,\"screen_access\":false,\"source_executable_opened\":false,\"passed\":true}\n";CHECK(out);
    std::cout<<"PASS exact integer FTOL versus cached body, full FP/stack and decline guards.\n";
}catch(const std::exception& error){a::enable_integer_ftol(true);std::cerr<<error.what()<<'\n';return 1;}}
