#pragma once
#include <winrecomp/compiled.hpp>
#include <utility>
namespace wa2::aot {
// Keep source integer values in ordinary C++ locals in the measured hot
// functions. The backing guest pages cannot alias this private register file.
// Commit on every external call and on unwind, preserving precise fault state.
template<bool Metered> class IntegerRegisters {
    wr::Cpu& cpu_;wr::Memory& memory_;wr::Memory::ExecutionScope& scope_;
    std::array<wr::U32,8> values_;bool active_{};
    static constexpr bool enabled_=!Metered;
    [[gnu::always_inline]] void load(){if(!active_){values_=cpu_.r;active_=true;}}
public:
    IntegerRegisters(wr::Cpu& cpu,wr::Memory& memory,wr::Memory::ExecutionScope& scope):cpu_(cpu),memory_(memory),scope_(scope){}
    ~IntegerRegisters(){flush();}
    [[gnu::always_inline]] void flush(){if(active_){cpu_.r=values_;active_=false;}}
    template<unsigned Index> [[gnu::always_inline]] wr::U32& word(){
        static_assert(Index<8);if(!enabled_)return cpu_.r[Index];load();return values_[Index];
    }
    template<unsigned Index,unsigned Width=32,unsigned Shift=0>
    [[gnu::always_inline]] wr::U32 get(){
        static_assert(Index<8 && (Width==8 || Width==16 || Width==32) && (Shift==0 || (Shift==8 && Width==8)));
        if(!enabled_)return wr::reg(cpu_,Index,Width,Shift);
        load();return (values_[Index]>>Shift)&wr::mask(Width);
    }
    template<unsigned Index,unsigned Width=32,unsigned Shift=0>
    [[gnu::always_inline]] void set(wr::U32 value){
        static_assert(Index<8 && (Width==8 || Width==16 || Width==32) && (Shift==0 || (Shift==8 && Width==8)));
        if(!enabled_){wr::set_reg(cpu_,Index,value,Width,Shift);return;}
        load();constexpr auto mask=(~wr::U32(0)>>(32-Width))<<Shift;
        values_[Index]=(values_[Index]&~mask)|((value<<Shift)&mask);
    }
    template<unsigned Width=32> [[gnu::always_inline]] void push(wr::U32 value){
        static_assert(Width==16 || Width==32);
        if(!enabled_){wr::push(cpu_,memory_,value,Width);return;}
        const auto address=get<wr::ESP>()-Width/8;
        scope_.template store<Width>(address,value);set<wr::ESP>(address);
    }
    template<unsigned Width=32> [[gnu::always_inline]] wr::U32 pop(){
        static_assert(Width==16 || Width==32);
        if(!enabled_)return wr::pop(cpu_,memory_,Width);
        const auto address=get<wr::ESP>(),value=scope_.template load<Width>(address);
        set<wr::ESP>(address+Width/8);return value;
    }
    template<class Function> [[gnu::always_inline]] decltype(auto) external(Function&& function){
        flush();
        // Argument expressions may read the cache again before the external
        // helper changes Cpu. Discard those reads on success AND on exception.
        struct Invalidate {IntegerRegisters& file;~Invalidate(){file.active_=false;}} invalidate{*this};
        return std::forward<Function>(function)();
    }
};
}
