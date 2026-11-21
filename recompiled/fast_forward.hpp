#pragma once
#include <winrecomp/memory.hpp>
#include "../engine/progress_layout.hpp"

namespace wa2::aot {
struct FastForwardSource {
    wr::U32 inhibit,ui_blocked,skip_mode,skip_latch,input_held,line,script,history,allow_unread,script_skip;
    wr::U32 current_state,previous_state,window_state,screen,idle_return,modal_screens;
};
inline constexpr FastForwardSource main_fast_forward_source{
    0x4be03c,0x4c2240,0xa39788,0x4be018,0x4be00c,0xa391e8,0xa391e4,0x567360,0x4be048,0xa391f4,
    0xa3a36c,0xa3a380,0x4c2234,0xa39208,0x4337d8,(1u<<6)|(1u<<7)};
// Read-only counterpart of cached 0x40ad10/0x405200. This never calls into
// source code from a device/scheduler boundary or changes its skip latch.
inline bool source_skip_requested(wr::Memory::ExecutionScope& memory,const FastForwardSource& source=main_fast_forward_source){
    if(memory.load<32>(source.inhibit) || memory.load<32>(source.ui_blocked))return false;
    if(!memory.load<32>(source.skip_mode) && !memory.load<32>(source.skip_latch))return false;
    if(memory.load<8>(source.input_held)&1)return true;
    const auto line=memory.load<32>(source.line);
    if(line==0xffffffffu)return false;
    const auto script=memory.load<32>(source.script);
    // Decline unfamiliar/overflowing history layouts. The original static
    // predicate remains responsible for them; do not guess an unread bit.
    if(script>=progress::layout::script_count || line>=progress::layout::lines_per_script-1)return false;
    const auto next=line+1;
    const auto seen=memory.load<8>(source.history+script*512+next/8)&(0x80u>>(next%8));
    return seen || memory.load<32>(source.allow_unread) || memory.load<32>(source.script_skip);
}
inline bool source_fast_forward(wr::Memory& memory,const FastForwardSource& source=main_fast_forward_source) noexcept {
    try{
        auto scope=memory.execution_scope();
        // Story state 3 also owns modal save/load screens (cached 0x41a783,
        // 0x41ef89). A retained skip key/latch must not accelerate their UI.
        const auto screen=scope.load<32>(source.screen);
        if(screen>=32 || (source.modal_screens&(1u<<screen)))return false;
        // Both current and last completed update must be story state 3,
        // matching 0x418f2b. Window transitions retain their ordinary pacing.
        return scope.load<32>(source.current_state)==3 && scope.load<32>(source.previous_state)==3 &&
            scope.load<32>(source.window_state)==0 && source_skip_requested(scope,source);
    }catch(const wr::GuestFault&){return false;}
}
inline bool source_fast_forward_idle(wr::Memory& memory,wr::U32 milliseconds,wr::U32 caller,const FastForwardSource& source=main_fast_forward_source) noexcept {
    // Only the main message loop's idle Sleep(2), following 0x418d90.
    // Device recovery, worker sleeps and all object waits keep their deadlines.
    return milliseconds==2 && caller==source.idle_return && source_fast_forward(memory,source);
}
}
