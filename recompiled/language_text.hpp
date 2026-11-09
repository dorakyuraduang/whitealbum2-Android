#pragma once
#include <winrecomp/runtime.hpp>
namespace wa2::aot {
// A source-level policy scoped to the current native invocation. No global
// machine-code patch, decoder, per-character registry or process ABI change.
struct SourceTextPolicy {wr::Memory* memory;bool ukrainian;};
inline thread_local SourceTextPolicy current_text_policy{};
class SourceTextScope {
    SourceTextPolicy previous_{current_text_policy};
public:
    SourceTextScope(wr::Memory& memory,bool ukrainian){current_text_policy={&memory,ukrainian};}
    ~SourceTextScope(){current_text_policy=previous_;}
    SourceTextScope(const SourceTextScope&)=delete;
};
inline bool ukrainian_text(wr::Memory& memory)noexcept{return current_text_policy.ukrainian && current_text_policy.memory==&memory;}
inline wr::U32 source_quote_byte(wr::Memory& memory)noexcept{return ukrainian_text(memory)?0x60u:0x27u;}
inline wr::U32 source_text_advance(wr::Cpu& cpu,wr::Memory& memory,wr::U32 stack,wr::U32 index,wr::U32 value){
    if(!ukrainian_text(memory))return value;
    const auto text=memory.load(stack+0x20,32);const auto code=memory.load(text+index,8);
    const unsigned amount=code==0x22?5u:(code==0x27 || code==0x3b || code==0x60)?6u:0u;
    if(amount)return wr::sub(cpu,value,amount,32);
    // The original trampoline's final CMP also sets flags on the ordinary
    // character path. Preserve that byte comparison, not the preceding ADD.
    (void)wr::sub(cpu,code,0x60,8);return value;
}
}
