#pragma once
#include <winrecomp/integer.hpp>
namespace wr::detail {
// Full arithmetic setters need only an operand descriptor until flags escape
// the block. Conditional branches can consume that descriptor directly.
class BlockFlags {
    Cpu& cpu;
    U32 left{},right{};unsigned width{};bool subtract{},pending{};
public:
    explicit BlockFlags(Cpu& value):cpu(value){}
    void set(U32 a,U32 b,unsigned bits,bool sub){left=a;right=b;width=bits;subtract=sub;pending=true;}
    void flush(){if(!pending)return;pending=false;if(subtract)wr::sub(cpu,left,right,width);else wr::add(cpu,left,right,width);}
    bool test(unsigned cc) const {
        if(!pending)return wr::test_condition(cpu,cc);
        const auto mask=wr::mask(width),sign=U32(1)<<(width-1);
        const auto a=left&mask,b=right&mask,result=(subtract?a-b:a+b)&mask;
        const auto code=cc&15;bool value{};
        switch(code>>1){
        case 0:value=((subtract?(a^b):~(a^b))&(a^result)&sign)!=0;break;
        case 1:value=subtract?a<b:std::uint64_t(a)+b>mask;break;
        case 2:value=result==0;break;
        case 3:value=(subtract?a<b:std::uint64_t(a)+b>mask)||result==0;break;
        case 4:value=(result&sign)!=0;break;
        case 5:value=(std::popcount(result&255u)&1)==0;break;
        case 6:case 7:
            if(subtract)value=wr::signed_value(a,width)<wr::signed_value(b,width);
            else value=bool(result&sign)!=bool((~(a^b)&(a^result)&sign));
            if((code>>1)==7)value=value||result==0;
            break;
        }
        return code&1?!value:value;
    }
    ~BlockFlags(){flush();}
};
}
