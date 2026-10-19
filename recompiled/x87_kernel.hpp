#pragma once
#include "extended80.hpp"
#include <winrecomp/x87.hpp>
#include <array>
#include <cmath>
#include <cstring>

namespace wa2::aot::fp {
namespace detail {
[[gnu::always_inline]] inline std::uint64_t read_word(const void* operand,unsigned bytes){
    if constexpr(std::endian::native==std::endian::little){
        if(bytes==8){std::uint64_t value;std::memcpy(&value,operand,8);return value;}
        if(bytes==4){std::uint32_t value;std::memcpy(&value,operand,4);return value;}
        if(bytes==2){std::uint16_t value;std::memcpy(&value,operand,2);return value;}
    }
    const auto* data=static_cast<const std::uint8_t*>(operand);std::uint64_t value{};
    for(unsigned i=0;i<bytes;++i)value|=std::uint64_t(data[i])<<(i*8);return value;
}
[[gnu::always_inline]] inline void write_word(void* operand,std::uint64_t value,unsigned bytes){
    if constexpr(std::endian::native==std::endian::little){
        if(bytes==8){std::memcpy(operand,&value,8);return;}
        if(bytes==4){const auto word=std::uint32_t(value);std::memcpy(operand,&word,4);return;}
        if(bytes==2){const auto word=std::uint16_t(value);std::memcpy(operand,&word,2);return;}
    }
    auto* data=static_cast<std::uint8_t*>(operand);for(unsigned i=0;i<bytes;++i)data[i]=std::uint8_t(value>>(i*8));
}
class Stack {
    wr::FpState& state_;
    unsigned initial_exceptions_{};
    bool underflow_{},overflow_{};
public:
    Environment environment;
    explicit Stack(void* state):state_(*static_cast<wr::FpState*>(state)),environment{state_.get16(0),state_.get16(2)}{
        initial_exceptions_=environment.status&0x3f;
        state_.clear_x87_padding();
    }
    ~Stack(){
        // A stack fault takes precedence over arithmetic exceptions and NaN
        // selection. Existing sticky exception bits still survive it.
        if(underflow_ || overflow_)environment.status=std::uint16_t(
            (environment.status&~(0x3f|RoundUp))|initial_exceptions_|Invalid|0x40|(overflow_ && !underflow_?RoundUp:0));
        state_.put16(2,environment.status);state_.put32(28,0xffff);
    }
    unsigned top()const noexcept{return (environment.status>>11)&7;}
    bool present(unsigned index)const noexcept{return state_.bytes[4]&(1u<<((top()+index)&7));}
    Extended raw(unsigned index)const{return {state_.get64(32+index*16),state_.get16(40+index*16)};}
    Extended get(unsigned index){
        if(present(index))return raw(index);
        underflow_=true;environment.raise(Invalid|0x40);environment.status&=~RoundUp;return indefinite();
    }
    void set(unsigned index,Extended value,bool canonical_fault=true){
        if(canonical_fault && (underflow_ || overflow_))value=indefinite();
        state_.put64(32+index*16,value.significand);state_.put16(40+index*16,value.sign_exponent);
        state_.clear_x87_padding(index);
        state_.bytes[4]|=std::uint8_t(1u<<((top()+index)&7));
    }
    void push(Extended value){
        const auto next=(top()+7)&7;
        if(state_.bytes[4]&(1u<<next)){overflow_=true;environment.raise(Invalid|0x40|RoundUp);value=indefinite();}
        std::array<std::uint8_t,16> tail;std::memcpy(tail.data(),state_.bytes.data()+144,16);
        std::memmove(state_.bytes.data()+48,state_.bytes.data()+32,112);std::memcpy(state_.bytes.data()+32,tail.data(),16);
        environment.status=std::uint16_t((environment.status&~0x3800)|(next<<11));set(0,value);
    }
    void pop(){
        state_.bytes[4]&=std::uint8_t(~(1u<<top()));
        const auto next=(top()+1)&7;
        std::array<std::uint8_t,16> head;std::memcpy(head.data(),state_.bytes.data()+32,16);
        std::memmove(state_.bytes.data()+32,state_.bytes.data()+48,112);std::memcpy(state_.bytes.data()+144,head.data(),16);
        environment.status=std::uint16_t((environment.status&~0x3800)|(next<<11));
    }
    void control(std::uint16_t value){
        value=std::uint16_t((value&0x1f3f)|0x40);
        state_.put16(0,value);environment.control=value;
        if(environment.status&~value&0x3f)environment.status|=0x8080;else environment.status&=~0x8080;
    }
    void compare_values(Extended left,Extended right,bool ordered){
        const auto result=fp::compare(left,right,environment,ordered);
        environment.status&=~(0x100|0x200|0x400|0x4000);
        environment.status|=std::uint16_t(result==2?0x4500:result<0?0x100:result==0?0x4000:0);
    }
    void examine(){
        const auto value=raw(0);unsigned kind;
        if(!present(0))kind=5;
        else if(value.unsupported())kind=0;
        else if(value.nan())kind=1;
        else if(value.infinity())kind=3;
        else if(value.zero())kind=4;
        else if(!value.exponent())kind=6;
        else kind=2;
        environment.status&=~(0x100|0x200|0x400|0x4000);
        environment.status|=std::uint16_t((kind&1?0x100:0)|(kind&2?0x400:0)|(kind&4?0x4000:0)|(value.sign()?0x200:0));
    }
};
inline Extended binary(unsigned kind,Extended left,Extended right,Environment& environment){
    switch(kind){
    case 0:return fp::add(left,right,environment);
    case 1:return fp::multiply(left,right,environment);
    case 4:return fp::subtract(left,right,environment);
    case 5:return fp::subtract(right,left,environment);
    case 6:return fp::divide(left,right,environment);
    case 7:return fp::divide(right,left,environment);
    default:throw std::logic_error("invalid compile-time x87 arithmetic kind");
    }
}
Extended transcend(unsigned operation,Extended first,Extended second,Environment&);
inline Extended constant(unsigned index,unsigned rounding){
    constexpr Extended values[]={
        {0x8000000000000000ull,0x3fff},{0xd49a784bcd1b8afeull,0x4000},
        {0xb8aa3b295c17f0bcull,0x3fff},{0xc90fdaa22168c235ull,0x4000},
        {0x9a209a84fbcff799ull,0x3ffd},{0xb17217f7d1cf79acull,0x3ffe},{0,0}};
    auto result=values[index];
    if(index>0 && index<6){
        if(rounding==1 || rounding==3){if(index!=1)--result.significand;}
        else if(rounding==2 && index==1)++result.significand;
    }
    return result;
}
}

// Opcode/encoding are compile-time constants in generated C++. There is no
// runtime opcode fetch or decode, and this is the same body on every host.
template<unsigned Opcode,unsigned Encoding,bool Memory,class Stack>
void x87_operation(Stack& stack,void* operand){
    auto& environment=stack.environment;
    if constexpr(Memory){
        constexpr auto digit=Encoding;
        if constexpr(Opcode==0xd8 || Opcode==0xdc || Opcode==0xda || Opcode==0xde){
            Extended right;
            auto operand_environment=environment;
            if constexpr(Opcode==0xd8 || Opcode==0xdc){
                constexpr auto bits=Opcode==0xd8?32u:64u;
                const auto raw=detail::read_word(operand,bits/8);
                right=from_binary(raw,bits,operand_environment);
                constexpr auto fraction_bits=bits==32?23u:52u;
                if(right.nan() && !(raw&(std::uint64_t(1)<<(fraction_bits-1))))right.significand&=~0x4000000000000000ull;
            }
            else if constexpr(Opcode==0xda)right=from_integer(std::bit_cast<std::int32_t>(std::uint32_t(detail::read_word(operand,4))));
            else right=from_integer(std::bit_cast<std::int16_t>(std::uint16_t(detail::read_word(operand,2))));
            const auto left=stack.get(0);
            if(!left.nan() && !left.unsupported() && !right.nan() && !right.unsupported())
                environment.raise(operand_environment.status&Denormal);
            if constexpr(digit==2 || digit==3){stack.compare_values(left,right,true);if constexpr(digit==3)stack.pop();}
            else stack.set(0,detail::binary(digit,left,right,environment));
        }else if constexpr(Opcode==0xd9 && digit==5){stack.control(std::uint16_t(detail::read_word(operand,2)));}
        else if constexpr(Opcode==0xd9 && digit==7){detail::write_word(operand,environment.control,2);}
        else if constexpr(Opcode==0xdd && digit==7){detail::write_word(operand,environment.status,2);}
        else if constexpr((Opcode==0xd9 || Opcode==0xdd) && digit==0){
            const auto value=from_binary(detail::read_word(operand,Opcode==0xd9?4:8),Opcode==0xd9?32:64,environment);stack.push(value);
        }else if constexpr((Opcode==0xd9 || Opcode==0xdd) && (digit==2 || digit==3)){
            detail::write_word(operand,to_binary(stack.get(0),Opcode==0xd9?32:64,environment),Opcode==0xd9?4:8);if constexpr(digit==3)stack.pop();
        }else if constexpr((Opcode==0xdb || Opcode==0xdf) && (digit==0 || (Opcode==0xdf && digit==5))){
            environment.status&=~RoundUp;
            if constexpr(Opcode==0xdb)stack.push(from_integer(std::bit_cast<std::int32_t>(std::uint32_t(detail::read_word(operand,4)))));
            else if constexpr(digit==0)stack.push(from_integer(std::bit_cast<std::int16_t>(std::uint16_t(detail::read_word(operand,2)))));
            else stack.push(from_integer(std::bit_cast<std::int64_t>(detail::read_word(operand,8))));
        }else if constexpr((Opcode==0xdb || Opcode==0xdf) && (digit==2 || digit==3 || (Opcode==0xdf && digit==7))){
            constexpr auto bits=Opcode==0xdb?32u:digit==7?64u:16u;
            detail::write_word(operand,to_integer(stack.get(0),bits,environment),bits/8);if constexpr(digit!=2)stack.pop();
        }else if constexpr(Opcode==0xdb && digit==5){
            environment.status&=~RoundUp;
            const auto* bytes=static_cast<const std::uint8_t*>(operand);
            stack.push({detail::read_word(bytes,8),std::uint16_t(detail::read_word(bytes+8,2))});
        }else if constexpr(Opcode==0xdb && digit==7){
            environment.status&=~RoundUp;const auto value=stack.get(0);auto* bytes=static_cast<std::uint8_t*>(operand);
            detail::write_word(bytes,value.significand,8);detail::write_word(bytes+8,value.sign_exponent,2);stack.pop();
        }else{static_assert(Opcode==0,"unimplemented static x87 memory form");}
    }else{
        constexpr auto digit=(Encoding>>3)&7,index=Encoding&7;
        if constexpr(Opcode==0xd8 || Opcode==0xdc || (Opcode==0xde && Encoding!=0xd9)){
            if constexpr(digit==2 || digit==3){stack.compare_values(stack.get(0),stack.get(index),true);if constexpr(digit==3)stack.pop();}
            else if constexpr(Opcode==0xd8)stack.set(0,detail::binary(digit,stack.get(0),stack.get(index),environment));
            else{
                constexpr auto operation=digit>=4?digit^1:digit;
                stack.set(index,detail::binary(operation,stack.get(index),stack.get(0),environment));if constexpr(Opcode==0xde)stack.pop();
            }
        }else if constexpr((Opcode==0xda && Encoding==0xe9) || (Opcode==0xde && Encoding==0xd9)){
            stack.compare_values(stack.get(0),stack.get(1),Opcode==0xde);stack.pop();stack.pop();
        }else if constexpr(Opcode==0xdd && (digit==4 || digit==5)){
            stack.compare_values(stack.get(0),stack.get(index),false);if constexpr(digit==5)stack.pop();
        }else if constexpr(Opcode==0xdd && (digit==2 || digit==3)){
            environment.status&=~RoundUp;stack.set(index,stack.get(0));if constexpr(digit==3)stack.pop();
        }else if constexpr(Opcode==0xd9 && digit==0){environment.status&=~RoundUp;const auto value=stack.get(index);stack.push(value);}
        else if constexpr(Opcode==0xd9 && digit==1){
            environment.status&=~RoundUp;const auto first=stack.get(0),other=stack.get(index);stack.set(0,other,false);stack.set(index,first,false);
        }else if constexpr(Opcode==0xd9 && Encoding>=0xe8 && Encoding<=0xee){
            environment.status&=~RoundUp;stack.push(detail::constant(Encoding-0xe8,environment.rounding()));
        }else if constexpr(Opcode==0xd9 && (Encoding==0xe0 || Encoding==0xe1)){
            environment.status&=~RoundUp;auto value=stack.get(0);
            if constexpr(Encoding==0xe0)value.sign_exponent^=0x8000;else value.sign_exponent&=0x7fff;stack.set(0,value);
        }else if constexpr(Opcode==0xd9 && Encoding==0xe4){stack.compare_values(stack.get(0),{},true);}
        else if constexpr(Opcode==0xd9 && Encoding==0xe5){stack.examine();}
        else if constexpr(Opcode==0xd9 && Encoding==0xfa){stack.set(0,square_root(stack.get(0),environment));}
        else if constexpr(Opcode==0xd9 && Encoding==0xfc){stack.set(0,round_integral(stack.get(0),environment));}
        else if constexpr(Opcode==0xd9 && Encoding==0xfd){stack.set(0,scale(stack.get(0),stack.get(1),environment));}
        else if constexpr(Opcode==0xd9 && (Encoding==0xf5 || Encoding==0xf8)){
            stack.set(0,remainder(stack.get(0),stack.get(1),environment,Encoding==0xf5));
        }else if constexpr(Opcode==0xd9 && (Encoding==0xfe || Encoding==0xff || Encoding==0xf2)){
            const auto value=stack.get(0);
            if(!value.infinity() && !value.nan() && int(value.exponent())-16383>=63){environment.status|=0x400;}
            else{
                environment.status&=~0x400;const auto result=detail::transcend(Encoding,value,{},environment);stack.set(0,result);
                if constexpr(Encoding==0xf2)stack.push(result.nan()?result:from_integer(1));
            }
        }else if constexpr(Opcode==0xd9 && (Encoding==0xf1 || Encoding==0xf3 || Encoding==0xf9)){
            stack.set(1,detail::transcend(Encoding,stack.get(0),stack.get(1),environment));stack.pop();
        }else if constexpr(Opcode==0xd9 && Encoding==0xf0){stack.set(0,detail::transcend(Encoding,stack.get(0),{},environment));}
        else if constexpr(Opcode==0xd9 && Encoding==0xd0){/* FNOP */}
        else{static_assert(Opcode==0,"unimplemented static x87 register form");}
    }
}
template<unsigned Opcode,unsigned Encoding,bool Memory>
void x87(void* state,void* operand,std::uint32_t*){
    detail::Stack stack(state);x87_operation<Opcode,Encoding,Memory>(stack,operand);
}
}
