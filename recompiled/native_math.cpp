#include "native_math.hpp"
#include "scalar_math.hpp"
#include "extended80.hpp"
#include <winrecomp/runtime.hpp>
#include <bit>
#include <cstring>
namespace wa2::aot {namespace {
bool argument(const wr::Cpu& s,unsigned index,double& value,bool exact=true,std::uint16_t* raised=nullptr){
    const fp::Extended original{s.fp.get64(32+index*16),s.fp.get16(40+index*16)};
    if(original.unsupported() || original.nan() || original.infinity())return false;
    if(original.zero()){value=std::bit_cast<double>(std::uint64_t(original.sign())<<63);return true;}
    const auto exponent=int(original.exponent())-16383;
    if(exponent>=-1022 && exponent<=1023 && !(original.significand&0x7ff)){
        value=std::bit_cast<double>((std::uint64_t(original.sign())<<63)|(std::uint64_t(exponent+1023)<<52)|((original.significand>>11)&0xfffffffffffffull));return true;
    }
    fp::Environment conversion;const auto bits=fp::to_binary(original,64,conversion);fp::Environment reverse;
    if(exact && fp::from_binary(bits,64,reverse)!=original)return false;
    if(raised)*raised=conversion.status&0x3f;
    value=std::bit_cast<double>(bits);return true;
}
bool admitted(const wr::Cpu& s,unsigned count){
#if WA2_AOT_DIAGNOSTICS
    if(!native_math_ports_enabled)return false;
#endif
    const auto control=s.fp.get16(0);const auto top=(s.fp.get16(2)>>11)&7;
    if((control&0x3f)!=0x3f || (control&0xc00))return false;
    for(unsigned n=0;n<count;++n)if(!(s.fp.bytes[4]&(1u<<((top+n)&7))))return false;
    return true;
}
void commit(wr::Cpu& s,wr::Memory& memory,wa2::math::ScalarResult result,unsigned count){
    const auto returning=memory.load(s.r[wr::ESP],32);
    fp::Environment environment{s.fp.get16(0),s.fp.get16(2)};
    const auto value=fp::from_binary(std::bit_cast<std::uint64_t>(result.value),64,environment);environment.raise(result.exceptions);
    if(count==2){
        std::uint8_t removed[16];std::memcpy(removed,s.fp.bytes.data()+32,16);
        std::memmove(s.fp.bytes.data()+32,s.fp.bytes.data()+48,112);std::memcpy(s.fp.bytes.data()+144,removed,16);
        const auto top=(environment.status>>11)&7;s.fp.bytes[4]&=std::uint8_t(~(1u<<top));
        environment.status=std::uint16_t((environment.status&~0x3800)|(((top+1)&7)<<11));
    }
    s.fp.put64(32,value.significand);s.fp.put16(40,value.sign_exponent);std::fill_n(s.fp.bytes.data()+42,6,0);
    s.fp.put16(2,environment.status);s.r[wr::ESP]+=4;s.eip=returning;
}
template<std::uint32_t Entry,auto Function> bool unary(wr::Cpu& s,wr::Memory& memory){
    if(s.eip!=Entry || !admitted(s,1))return false;
    // The ordinary source SSE path uses nearest IEEE arithmetic. Other modes
    // retain the original statically compiled function, without state changes.
    const auto mxcsr=s.fp.get32(24);if((mxcsr&0x1f80)!=0x1f80 || (mxcsr&0xe040))return false;
    if((s.fp.get16(0)&0x7f)!=0x7f || !memory.load(0xbc830c,32))return false;
    // This dispatch prefix stores ST0 as binary64 before the SSE polynomial.
    // Preserve that explicit rounding for extended intermediate phase values.
    double input{};std::uint16_t conversion{};if(!argument(s,0,input,false,&conversion))return false;
    if(!wa2::math::exact_trigonometry_input(input))return false;
    // Source data is normally immutable. A modified table must still take
    // the original compiled body, never a stale native replacement.
    auto scope=memory.execution_scope();
    constexpr auto table=Entry==0x474990?0x4a84d0u:0x4ac710u;
    const auto expected=wa2::math::trigonometry_source_words();
    const auto actual=scope.readable(table,expected.size_bytes());
    if(actual.size()!=expected.size_bytes() || std::memcmp(actual.data(),expected.data(),expected.size_bytes()))return false;
    auto result=Function(input);result.exceptions|=conversion;
    commit(s,memory,result,1);
#if WA2_AOT_DIAGNOSTICS
    if constexpr(Entry==0x474990)++native_math_test_counts.cosine;else ++native_math_test_counts.sine;
#endif
    return true;
}
}
#if WA2_AOT_DIAGNOSTICS
thread_local bool native_math_ports_enabled=true;
thread_local NativeMathTestCounts native_math_test_counts;
#endif
bool native_atan2(wr::Cpu& s,wr::Memory& memory){
    if(s.eip!=0x47885a || !admitted(s,2))return false;
    double x{},y{};if(!argument(s,0,x) || !argument(s,1,y))return false;
    const auto result=wa2::math::atan2_finite(y,x);
    commit(s,memory,result,2);
#if WA2_AOT_DIAGNOSTICS
    ++native_math_test_counts.atan2;
#endif
    return true;
}
bool native_sine(wr::Cpu& s,wr::Memory& memory){return unary<0x474d50,wa2::math::sine_finite>(s,memory);}
bool native_cosine(wr::Cpu& s,wr::Memory& memory){return unary<0x474990,wa2::math::cosine_finite>(s,memory);}
}
