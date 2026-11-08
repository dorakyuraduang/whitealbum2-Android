#pragma once
#include <winrecomp/memory.hpp>
#include "../engine/progress_layout.hpp"

namespace wa2::aot {
// Read-only counterpart of cached 0x40ad10/0x405200. This never calls into
// source code from a device/scheduler boundary or changes its skip latch.
inline bool source_skip_requested(wr::Memory::ExecutionScope& memory){
    if(memory.load<32>(0x4be03c) || memory.load<32>(0x4c2240))return false;
    if(!memory.load<32>(0xa39788) && !memory.load<32>(0x4be018))return false;
    if(memory.load<8>(0x4be00c)&1)return true;
    const auto line=memory.load<32>(0xa391e8);
    if(line==0xffffffffu)return false;
    const auto script=memory.load<32>(0xa391e4);
    // Decline unfamiliar/overflowing history layouts. The original static
    // predicate remains responsible for them; do not guess an unread bit.
    if(script>=progress::layout::script_count || line>=progress::layout::lines_per_script-1)return false;
    const auto next=line+1;
    const auto seen=memory.load<8>(0x567360+script*512+next/8)&(0x80u>>(next%8));
    return seen || memory.load<32>(0x4be048) || memory.load<32>(0xa391f4);
}
inline bool source_fast_forward(wr::Memory& memory) noexcept {
    try{
        auto scope=memory.execution_scope();
        // Story state 3 also owns modal save/load screens (cached 0x41a783,
        // 0x41ef89). A retained skip key/latch must not accelerate their UI.
        const auto screen=scope.load<32>(0xa39208);
        if(screen==6 || screen==7)return false;
        // Both current and last completed update must be story state 3,
        // matching 0x418f2b. Window transitions retain their ordinary pacing.
        return scope.load<32>(0xa3a36c)==3 && scope.load<32>(0xa3a380)==3 &&
            scope.load<32>(0x4c2234)==0 && source_skip_requested(scope);
    }catch(const wr::GuestFault&){return false;}
}
inline bool source_fast_forward_idle(wr::Memory& memory,wr::U32 milliseconds,wr::U32 caller) noexcept {
    // Only the main message loop's idle Sleep(2), following 0x418d90.
    // Device recovery, worker sleeps and all object waits keep their deadlines.
    return milliseconds==2 && caller==0x4337d8 && source_fast_forward(memory);
}
}
