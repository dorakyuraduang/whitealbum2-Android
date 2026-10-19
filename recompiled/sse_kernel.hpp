#pragma once
#include "x87_kernel.hpp"
#include <winrecomp/runtime.hpp>

namespace wa2::aot::fp {
namespace detail {
using Vector=std::array<std::uint8_t,16>;
inline Vector vector(const wr::FpState& state,unsigned index){
    Vector result;std::copy_n(state.bytes.data()+160+index*16,16,result.data());return result;
}
inline void vector(wr::FpState& state,unsigned index,const Vector& value){std::copy(value.begin(),value.end(),state.bytes.begin()+160+index*16);}
inline std::uint64_t lane(const Vector& value,unsigned index,unsigned bytes=8){return read_word(value.data()+index*bytes,bytes);}
inline void set_lane(Vector& value,unsigned index,std::uint64_t bits,unsigned bytes=8){write_word(value.data()+index*bytes,bits,bytes);}
inline Environment simd_environment(std::uint32_t mxcsr){return {std::uint16_t(0x27f|(((mxcsr>>13)&3)<<10)),0,-1022,1023};}
inline Extended simd_input(std::uint64_t bits,std::uint32_t mxcsr,Environment& environment){
    if((mxcsr&0x40) && !(bits&0x7ff0000000000000ull) && (bits&0xfffffffffffffull))bits&=0x8000000000000000ull;
    return from_binary(bits,64,environment);
}
inline std::uint64_t simd_output(Extended value,std::uint32_t mxcsr,Environment& environment){
    auto result=to_binary(value,64,environment);
    if((mxcsr&0x8000) && !(result&0x7ff0000000000000ull) && (result&0xfffffffffffffull)){
        environment.raise(Underflow|Inexact);result&=0x8000000000000000ull;
    }
    return result;
}
inline Extended simd_nan(Extended first,Extended second,Environment& environment){
    environment.status&=~Denormal;
    auto result=first.nan()?first:second;result.significand|=0xc000000000000000ull;return result;
}
inline void simd_finish(wr::FpState& state,std::uint32_t mxcsr,unsigned raised){
    state.put32(24,mxcsr|(raised&0x3f));state.put32(28,0xffff);
    state.clear_x87_padding();
}
}

template<unsigned Prefix,unsigned Opcode,unsigned Encoding,bool Memory,unsigned Immediate=0>
void sse(void* raw,void* operand,std::uint32_t* flags){
    auto& state=*static_cast<wr::FpState*>(raw);
    constexpr unsigned reg=Memory?Encoding:Encoding/8,rm=Encoding&7;
    const auto left=detail::vector(state,reg);
    detail::Vector right{};
    if constexpr(Memory)std::copy_n(static_cast<const std::uint8_t*>(operand),16,right.data());
    else right=detail::vector(state,rm);
    auto result=left;const auto mxcsr=state.get32(24);unsigned raised{};
    if constexpr(Opcode==0x28 || Opcode==0x6f || (Opcode==0x10 && Prefix==0x66))result=right;
    else if constexpr(Opcode==0x29 || Opcode==0x7f || Opcode==0x11 || (Opcode==0xd6 && Prefix==0x66)){
        constexpr unsigned bytes=(Opcode==0xd6 || (Opcode==0x11 && Prefix==0xf2))?8:16;
        if constexpr(Memory){std::copy_n(left.data(),bytes,static_cast<std::uint8_t*>(operand));detail::simd_finish(state,mxcsr,0);return;}
        else{
            auto destination=detail::vector(state,rm);std::copy_n(left.data(),bytes,destination.data());
            if constexpr(Opcode==0xd6)std::fill(destination.begin()+8,destination.end(),0);
            detail::vector(state,rm,destination);detail::simd_finish(state,mxcsr,0);return;
        }
    }else if constexpr(Opcode==0x10 && Prefix==0xf2){
        detail::set_lane(result,0,detail::lane(right,0));if constexpr(Memory)detail::set_lane(result,1,0);
    }else if constexpr(Opcode==0x7e && Prefix==0xf3){detail::set_lane(result,0,detail::lane(right,0));detail::set_lane(result,1,0);}
    else if constexpr(Opcode==0x12 && Prefix==0x66){detail::set_lane(result,0,detail::lane(right,0));}
    else if constexpr(Opcode==0x13 && Prefix==0x66){
        static_assert(Memory);detail::write_word(operand,detail::lane(left,0),8);detail::simd_finish(state,mxcsr,0);return;
    }else if constexpr(Opcode==0x14){detail::set_lane(result,1,detail::lane(right,0));}
    else if constexpr(Opcode==0x15){detail::set_lane(result,0,detail::lane(left,1));detail::set_lane(result,1,detail::lane(right,1));}
    else if constexpr(Opcode==0x54 || Opcode==0x55 || Opcode==0x56 || Opcode==0x57 || Opcode==0xef){
        for(unsigned i=0;i<16;++i){
            if constexpr(Opcode==0x54)result[i]=left[i]&right[i];
            if constexpr(Opcode==0x55)result[i]=std::uint8_t(~left[i])&right[i];
            if constexpr(Opcode==0x56)result[i]=left[i]|right[i];
            if constexpr(Opcode==0x57 || Opcode==0xef)result[i]=left[i]^right[i];
        }
    }else if constexpr(Opcode==0xfa || Opcode==0xfb || Opcode==0x76 || Opcode==0xee){
        constexpr unsigned bytes=Opcode==0xfb?8:Opcode==0xee?2:4;
        for(unsigned i=0;i<16/bytes;++i){
            const auto a=detail::lane(left,i,bytes),b=detail::lane(right,i,bytes);
            if constexpr(Opcode==0x76)detail::set_lane(result,i,a==b?0xffffffff:0,4);
            else if constexpr(Opcode==0xee)detail::set_lane(result,i,std::uint16_t(std::max(std::bit_cast<std::int16_t>(std::uint16_t(a)),std::bit_cast<std::int16_t>(std::uint16_t(b)))),2);
            else detail::set_lane(result,i,a-b,bytes);
        }
    }else if constexpr(Opcode==0xd3 || Opcode==0xf3){
        const auto count=detail::lane(right,0);
        for(unsigned i=0;i<2;++i){const auto value=detail::lane(left,i);detail::set_lane(result,i,count>=64?0:Opcode==0xd3?value>>count:value<<count);}
    }else if constexpr(Opcode==0xe6 && Prefix==0xf3){
        for(unsigned i=0;i<2;++i){auto environment=detail::simd_environment(mxcsr);detail::set_lane(result,i,detail::simd_output(from_integer(std::bit_cast<std::int32_t>(std::uint32_t(detail::lane(right,i,4)))),mxcsr,environment));raised|=environment.status;}
    }else if constexpr(Opcode==0x2a && Prefix==0xf2){
        auto environment=detail::simd_environment(mxcsr);const auto value=from_integer(std::bit_cast<std::int32_t>(std::uint32_t(detail::lane(right,0,4))));
        detail::set_lane(result,0,detail::simd_output(value,mxcsr,environment));raised|=environment.status;
    }else if constexpr(Opcode==0x2e || Opcode==0x2f){
        auto environment=detail::simd_environment(mxcsr);const auto a=detail::simd_input(detail::lane(left,0),mxcsr,environment),b=detail::simd_input(detail::lane(right,0),mxcsr,environment);
        if(a.nan() || b.nan())environment.status&=~Denormal;
        const auto relation=compare(a,b,environment,Opcode==0x2f);
        *flags=(*flags&~wr::STATUS_FLAGS)|(relation==2?wr::CF|wr::PF|wr::ZF:relation<0?wr::CF:relation==0?wr::ZF:0);
        detail::simd_finish(state,mxcsr,environment.status);return;
    }else if constexpr(Opcode==0xc2){
        for(unsigned i=0;i<(Prefix==0xf2?1u:2u);++i){
            auto environment=detail::simd_environment(mxcsr);const auto a=detail::simd_input(detail::lane(left,i),mxcsr,environment),b=detail::simd_input(detail::lane(right,i),mxcsr,environment);
            if(a.nan() || b.nan())environment.status&=~Denormal;
            constexpr unsigned predicate=Immediate&7;const auto relation=compare(a,b,environment,predicate==1 || predicate==2 || predicate==5 || predicate==6);
            bool value;
            if constexpr(predicate==0)value=relation==0;
            if constexpr(predicate==1)value=relation==-1;
            if constexpr(predicate==2)value=relation==-1 || relation==0;
            if constexpr(predicate==3)value=relation==2;
            if constexpr(predicate==4)value=relation!=0;
            if constexpr(predicate==5)value=relation!=-1;
            if constexpr(predicate==6)value=relation!=0 && relation!=-1;
            if constexpr(predicate==7)value=relation!=2;
            detail::set_lane(result,i,value?~std::uint64_t(0):0);raised|=environment.status;
        }
    }else if constexpr(Opcode==0x58 || Opcode==0x59 || Opcode==0x5c || Opcode==0x5e || Opcode==0x51){
        for(unsigned i=0;i<(Prefix==0xf2?1u:2u);++i){
            auto environment=detail::simd_environment(mxcsr);
            const auto b=detail::simd_input(detail::lane(right,i),mxcsr,environment);
            Extended value;
            if constexpr(Opcode==0x51)value=square_root(b,environment);
            else{
                const auto a=detail::simd_input(detail::lane(left,i),mxcsr,environment);
                if(a.nan() || b.nan())value=detail::simd_nan(a,b,environment);
                else if constexpr(Opcode==0x58)value=add(a,b,environment);
                else if constexpr(Opcode==0x59)value=multiply(a,b,environment);
                else if constexpr(Opcode==0x5c)value=subtract(a,b,environment);
                else value=divide(a,b,environment);
            }
            detail::set_lane(result,i,detail::simd_output(value,mxcsr,environment));raised|=environment.status;
        }
    }else{static_assert(Opcode==0,"unimplemented static SIMD form");}
    detail::vector(state,reg,result);detail::simd_finish(state,mxcsr,raised);
}

template<unsigned Opcode,unsigned Source,bool Memory>
void sse_to_i32(void* raw,void* operand,std::uint32_t*){
    auto& state=*static_cast<wr::FpState*>(raw);const auto mxcsr=state.get32(24);auto environment=detail::simd_environment(mxcsr);
    const auto bits=Memory?detail::read_word(operand,8):state.get64(160+Source*16);
    const auto input=detail::simd_input(bits,mxcsr,environment);environment.status&=~Denormal;
    const auto value=to_integer(input,32,environment,Opcode==0x2c);detail::write_word(operand,value,4);
    detail::simd_finish(state,mxcsr,environment.status);
}
}
