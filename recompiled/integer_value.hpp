#pragma once
#include <winrecomp/compiled.hpp>
namespace wa2::aot::value {
// Only generated liveness proofs select Flags=false. Diagnostic bodies keep
// the original helpers. No memory or control-flow operation is rewritten.
template<bool Flags> inline wr::U32 add(wr::Cpu& s,wr::U32 a,wr::U32 b,unsigned width,wr::U32 carry=0){
    if constexpr(Flags)return wr::add(s,a,b,width,carry);
    else return (a+b+(carry&1u))&wr::mask(width);
}
template<bool Flags> inline wr::U32 sub(wr::Cpu& s,wr::U32 a,wr::U32 b,unsigned width,wr::U32 borrow=0){
    if constexpr(Flags)return wr::sub(s,a,b,width,borrow);
    else return (a-b-(borrow&1u))&wr::mask(width);
}
template<bool Flags> inline wr::U32 logic(wr::Cpu& s,wr::U32 result,unsigned width){
    if constexpr(Flags)return wr::logic(s,result,width);
    else return result&wr::mask(width);
}
template<bool Flags> inline wr::U32 incdec(wr::Cpu& s,wr::U32 a,unsigned width,bool decrement){
    if constexpr(Flags)return wr::incdec(s,a,width,decrement);
    else return (decrement?a-1u:a+1u)&wr::mask(width);
}
template<bool Flags> inline wr::U32 imul(wr::Cpu& s,wr::U32 a,wr::U32 b,unsigned width){
    if constexpr(Flags)return wr::imul(s,a,b,width);
    else return std::uint32_t(std::uint64_t(a)*b)&wr::mask(width);
}
}
