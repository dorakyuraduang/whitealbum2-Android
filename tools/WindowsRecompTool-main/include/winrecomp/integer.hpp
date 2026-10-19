#pragma once
#include "winrecomp/runtime.hpp"
namespace wr {
inline void tick(std::uint64_t& budget,U32 pc) {
    if(!budget)throw GuestFault(FaultKind::budget,pc,"execution budget exhausted");
    --budget;
}
inline std::int64_t signed_value(U32 v,unsigned w) {return std::bit_cast<std::int32_t>(sign_extend(v,w));}
// kind: SHL=0 SHR=1 SAR=2 ROL=3 ROR=4 RCL=5 RCR=6.
inline U32 shift(Cpu& s,U32 a,U32 count,unsigned w,unsigned kind) {
    a&=mask(w);const U32 sign=U32(1)<<(w-1);const unsigned masked=count&31u;
    if(!masked)return a;
    unsigned n=masked;U32 carry=s.flags&CF;
    if(kind>=5 && w<32)n%=w+1;
    if(!n) {
        // Only a zero MASKED count preserves OF. A nonzero count that becomes
        // zero modulo 9/17 still leaves OF undefined (RCL/RCR full cycles).
        // Preserve CF and all other flags and the destination in this case.
        s.defined_flags&=~OF;
        return a;
    }
    const U32 old=a;
    if(kind==0) {
        const auto wide=std::uint64_t(a)<<n;
        a=U32(wide)&mask(w);carry=U32(wide>>w)&1u;
    }else if(kind==1) {
        carry=(a>>(n-1))&1u;a>>=n;
    }else if(kind==2) {
        if(n>=w){carry=bool(a&sign);a=carry?mask(w):0u;}
        else {carry=(a>>(n-1))&1u;a=(a>>n)|((a&sign)?(mask(w)^(mask(w)>>n)):0u);}
    }else if(kind==3 || kind==4) {
        // A full rotation still updates CF when the masked count is nonzero.
        // Widening also makes the zero-remainder shifts well-defined at w=32.
        const auto r=n%w;const auto wide=std::uint64_t(a);
        a=U32(kind==3?(wide<<r)|(wide>>(w-r)):(wide>>r)|(wide<<(w-r)))&mask(w);
        carry=kind==3?(a&1u):U32(bool(a&sign));
    }else if(kind==5 || kind==6) {
        const auto bits=w+1;const auto wide=(std::uint64_t(carry)<<w)|a;
        const auto rotated=(kind==5?(wide<<n)|(wide>>(bits-n)):(wide>>n)|(wide<<(bits-n)))&((std::uint64_t(1)<<bits)-1);
        a=U32(rotated)&mask(w);carry=U32(rotated>>w)&1u;
    }else {
        carry=(a>>(n-1))&1u;a>>=n;
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
inline U32 double_shift(Cpu& s,U32 a,U32 b,U32 count,unsigned w,bool right) {
    const unsigned n=count&31u;a&=mask(w);b&=mask(w);if(!n)return a;
    if(n>w)throw GuestFault(FaultKind::unsupported,s.eip,"undefined SHLD/SHRD destination");
    const std::uint64_t wide=right?(std::uint64_t(b)<<w)|a:(std::uint64_t(a)<<w)|b;
    const U32 result=right?U32(wide>>n)&mask(w):U32((wide<<n)>>w)&mask(w);
    const U32 carry=right?((a>>(n-1))&1u):((a>>(w-n))&1u);
    s.flags=(s.flags&~(SF|ZF|PF|CF))|szp(result,w)|(carry?CF:0u);
    s.defined_flags=(s.defined_flags|SF|ZF|PF|CF)&~AF;
    if(n==1){const auto of=((a^result)&(U32(1)<<(w-1)))?OF:0u;s.flags=(s.flags&~OF)|of;s.defined_flags|=OF;}
    else s.defined_flags&=~OF;
    return result;
}
inline void multiply_flags(Cpu& s,bool overflow) {
    s.flags=(s.flags&~(CF|OF))|(overflow?(CF|OF):0u);
    s.defined_flags=(s.defined_flags&~STATUS_FLAGS)|CF|OF;
}
inline U32 imul(Cpu& s,U32 a,U32 b,unsigned w) {
    const auto product=signed_value(a,w)*signed_value(b,w);const U32 low=U32(product)&mask(w);
    multiply_flags(s,product!=signed_value(low,w));return low;
}
inline void multiply(Cpu& s,U32 b,unsigned w,bool is_signed) {
    const U32 a=reg(s,EAX,w);
    const std::uint64_t product=is_signed?std::uint64_t(signed_value(a,w)*signed_value(b,w)):std::uint64_t(a)*(b&mask(w));
    const U32 low=U32(product)&mask(w),high=U32(product>>w)&mask(w);
    const bool overflow=is_signed?(high!=(low&(U32(1)<<(w-1))?mask(w):0u)):(high!=0);
    if(w==8)set_reg(s,EAX,U32(product),16);else {set_reg(s,EAX,low,w);set_reg(s,EDX,high,w);}
    multiply_flags(s,overflow);
}
inline void divide(Cpu& s,U32 divisor,unsigned w,bool is_signed) {
    divisor&=mask(w);if(!divisor)throw GuestFault(FaultKind::divide,s.eip,"division by zero");
    const std::uint64_t dividend=w==8?reg(s,EAX,16):(std::uint64_t(reg(s,EDX,w))<<w)|reg(s,EAX,w);
    U32 quotient{},remainder{};
    if(is_signed) {
        const auto a=w==32?std::bit_cast<std::int64_t>(dividend):signed_value(U32(dividend),w*2);
        const auto b=signed_value(divisor,w);
        if(a==std::numeric_limits<std::int64_t>::min() && b==-1)throw GuestFault(FaultKind::divide,s.eip,"signed division overflow");
        const auto q=a/b,r=a%b;
        const auto limit=std::int64_t(1)<<(w-1);
        if(q < -limit || q>=limit)throw GuestFault(FaultKind::divide,s.eip,"division quotient overflow");
        quotient=U32(q);remainder=U32(r);
    }else {
        const auto q=dividend/divisor;
        if(q>mask(w))throw GuestFault(FaultKind::divide,s.eip,"division quotient overflow");
        quotient=U32(q);remainder=U32(dividend%divisor);
    }
    if(w==8){set_reg(s,EAX,quotient,8);set_reg(s,EAX,remainder,8,8);}
    else {set_reg(s,EAX,quotient,w);set_reg(s,EDX,remainder,w);}
    s.defined_flags&=~STATUS_FLAGS;
}
inline bool test_condition(const Cpu& s,unsigned cc) {
    const bool c=s.flags&CF,z=s.flags&ZF,o=s.flags&OF,sign=s.flags&SF,p=s.flags&PF;
    switch(cc&15){case 0:return o;case 1:return !o;case 2:return c;case 3:return !c;
    case 4:return z;case 5:return !z;case 6:return c||z;case 7:return !(c||z);
    case 8:return sign;case 9:return !sign;case 10:return p;case 11:return !p;
    case 12:return sign!=o;case 13:return sign==o;case 14:return z||sign!=o;default:return !z&&sign==o;}
}
inline U32 scan(Cpu& s,U32 source,U32 old,unsigned w,bool reverse) {
    source&=mask(w);s.defined_flags=(s.defined_flags&~STATUS_FLAGS)|ZF;
    if(!source){s.flags|=ZF;return old;}// Architecturally undefined destination retained.
    s.flags&=~ZF;return reverse?31u-unsigned(std::countl_zero(source)):unsigned(std::countr_zero(source));
}
inline U32 bit_op(Cpu& s,U32 value,U32 bit,unsigned w,unsigned kind) {
    bit%=w;const U32 m=U32(1)<<bit;
    s.flags=(s.flags&~CF)|((value&m)?CF:0u);
    s.defined_flags=(s.defined_flags&~(OF|SF|AF|PF))|CF;
    if(kind==1)return value|m;if(kind==2)return value&~m;if(kind==3)return value^m;return value;
}
inline U32 bit_address(U32 base,U32 bit,unsigned w) {
    const auto b=signed_value(bit,w);
    const auto words=b>=0?b/std::int64_t(w):-((-b+std::int64_t(w)-1)/std::int64_t(w));
    return base+U32(words*std::int64_t(w/8));
}
// kind MOVS=0 STOS=1 LODS=2 CMPS=3 SCAS=4. repeat 0=none 1=REP/REPE 2=REPNE.
template<bool Metered=true>
inline void string_op(Cpu& s,Memory& m,unsigned kind,unsigned w,unsigned address_width,unsigned repeat,unsigned segment,std::uint64_t& budget) {
    const U32 am=mask(address_width),step=w/8;
    for(;;) {
        if(repeat && !reg(s,ECX,address_width))return;
        if constexpr(Metered)tick(budget,s.eip);
        const U32 si=reg(s,ESI,address_width),di=reg(s,EDI,address_width);
        const U32 source=segment_address(s,si,segment);
        if(kind==0){auto v=m.load(source,w);m.store(di,v,w);}
        else if(kind==1)m.store(di,reg(s,EAX,w),w);
        else if(kind==2)set_reg(s,EAX,m.load(source,w),w);
        else {auto a=kind==3?m.load(source,w):reg(s,EAX,w);auto b=m.load(di,w);(void)sub(s,a,b,w);}
        const U32 delta=(s.flags&DF)?0u-step:step;
        if(kind==0 || kind==2 || kind==3)set_reg(s,ESI,(si+delta)&am,address_width);
        if(kind==0 || kind==1 || kind==3 || kind==4)set_reg(s,EDI,(di+delta)&am,address_width);
        if(!repeat)return;
        set_reg(s,ECX,(reg(s,ECX,address_width)-1)&am,address_width);
        if(kind>=3 && ((repeat==1 && !(s.flags&ZF)) || (repeat==2 && (s.flags&ZF))))return;
    }
}
}
