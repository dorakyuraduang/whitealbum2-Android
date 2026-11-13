#pragma once
#include "text_ftol.hpp"
#include "fp_registers.hpp"
namespace wa2::aot::fp {
// A statically selected text conversion can consume the already-live x87
// register file. No serialized save/reload is needed around an admitted call.
// Unsafe inputs decline before any state changes and retain the source call.
struct TextFtolAdapter {
    template<bool Metered> static bool invoke(wr::Cpu& s,wr::Memory& m,wr::Memory::ExecutionScope& scope,RegisterFrame<Metered>& frame){
        if constexpr(Metered)return false;
        if(&frame.cpu_!=&s || &frame.memory_!=&m || &frame.scope_!=&scope || !frame.enabled())return false;
        if(!frame.active_)return native_integer_ftol(s,m,scope);
        TextFtolPlan plan;
        if(!prepare_text_ftol(s,scope,frame.stack_.raw(0),frame.stack_.environment.control,
            frame.stack_.environment.status,frame.stack_.present(0),plan))return false;
        frame.stack_.environment.status&=~RoundUp;frame.stack_.pop();
        frame.instruction_=0x474f12;frame.data_=plan.operand;frame.opcode_=1308&0x7ff;
        apply_text_ftol(s,scope,plan);return true;
    }
};
}
