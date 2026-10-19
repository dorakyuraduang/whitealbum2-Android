// Target-specific Windows API bridges. All guest pointers remain 32-bit and
// buffers are checked/copied; no host pointer is passed back to guest code.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winrecomp/process.hpp>
#include <winrecomp/win32_state.hpp>
#include <winrecomp/win64_abi.hpp>
#include <bit>
#include <map>
#include <memory>
#include "wa2_win32.hpp"
#include "fast_paths.hpp"
#include "static_blocks.hpp"
#include "format.hpp"
#include "native_resources.hpp"
#include "native_vorbis.hpp"
#include "clock.hpp"

namespace wr {
namespace {
struct TargetState {
    struct FiberSlot {std::map<U32,U32> values;U32 callback{};};
    std::map<U32,FiberSlot> fiber_slots;
    U32 pointer_cookie{};
    wa2::platform::TimerResolution timers;
};
void store_u64(Process& p,U32 at,std::uint64_t value) {
    p.memory.check(at,8,Memory::Write);
    p.memory.store(at,U32(value),32);p.memory.store(at+4,U32(value>>32),32);
}
}
void install_wa2_win32(Process& p) {
    p.memory.enable_linear_memory();
    p.state().reports["guest_allocations"]=[&p]{
        const auto regions=p.memory.regions();std::uint64_t reserved=0,requested=0,blocks=0,pools=0,direct_bytes=0;
        for(const auto& region:regions)reserved+=region.size;
        for(const auto& [id,heap]:p.state().heaps){
            requested+=heap.requested_bytes;blocks+=heap.blocks.size();pools+=heap.pools.size();
            for(const auto& [pointer,block]:heap.blocks)if(!block.pool)direct_bytes+=block.capacity;
        }
        const auto native=p.memory.native_operations();
        return "{\"version\":\"bulk-resource-memory-2026-10-16\",\"native_commit_calls\":"+std::to_string(native.commits)+
            ",\"native_decommit_calls\":"+std::to_string(native.decommits)+",\"native_committed_pages\":"+std::to_string(native.committed_pages)+
            ",\"regions\":"+std::to_string(regions.size())+
            ",\"reserved_bytes\":"+std::to_string(reserved)+",\"heap_requested_bytes\":"+std::to_string(requested)+
            ",\"heap_blocks\":"+std::to_string(blocks)+",\"heap_pools\":"+std::to_string(pools)+
            ",\"heap_pool_bytes\":"+std::to_string(pools*65536)+",\"heap_direct_bytes\":"+std::to_string(direct_bytes)+"}";
    };
    install_wa2_shell(p);
    install_wa2_kernel(p);
    install_wa2_registry(p);
    install_wa2_direct3d(p);
    install_wa2_audio(p);
    install_wa2_acm(p);
    install_wa2_media(p);
    auto state=std::make_shared<TargetState>();
    const auto seed=wa2::platform::Clock::counter();
    state->pointer_cookie=U32(seed)^U32(seed>>32)^0x9e3779b9u;
    if(!state->pointer_cookie)state->pointer_cookie=0xa5b35719u;
    p.register_api("user32.dll","wvsprintfA",3,[](Process& q)->U32 {return format_guest_ansi(q,q.argument(0),q.argument(1),q.argument(2));});
    p.register_api("user32.dll","wsprintfA",2,[](Process& q)->U32 {return format_guest_ansi(q,q.argument(0),q.argument(1),q.cpu.r[ESP]+12);},true);
    p.register_api("winmm.dll","timeGetTime",0,[](Process& q)->U32 {return wa2::platform::Clock::milliseconds_since(q.state().clock_start);});
    p.register_api("winmm.dll","timeGetDevCaps",2,[](Process& q)->U32 {
        if(q.argument(1)!=8)return 97;
        const auto at=q.argument(0);q.memory.check(at,8,Memory::Write);const auto caps=wa2::platform::TimerResolution::caps();
        if(!caps.valid)return 97;
        q.memory.store(at,caps.minimum,32);q.memory.store(at+4,caps.maximum,32);return 0;
    });
    p.register_api("winmm.dll","timeBeginPeriod",1,[state](Process& q)->U32 {
        return state->timers.begin(q.argument(0))?0:97;
    });
    p.register_api("winmm.dll","timeEndPeriod",1,[state](Process& q)->U32 {
        return state->timers.end(q.argument(0))?0:97;
    });
    p.register_api("kernel32.dll","IsValidCodePage",1,[](Process& q)->U32 {return U32(::IsValidCodePage(q.argument(0)));});
    p.register_api("kernel32.dll","IsValidLocale",2,[](Process& q)->U32 {return U32(::IsValidLocale(q.argument(0),q.argument(1)));});
    p.register_api("kernel32.dll","SetUnhandledExceptionFilter",1,[state](Process& q)->U32 {
        const auto value=q.argument(0);if(value)q.memory.check(value,1,Memory::Execute);const auto previous=q.state().unhandled_filter;q.state().unhandled_filter=value;return previous;
    });
    p.register_api("kernel32.dll","GetModuleHandleW",1,[](Process& q)->U32 {
        const auto at=q.argument(0);if(!at)return q.image_base;
        const auto wide=q.read_wstring(at);std::string name;
        for(auto ch:wide){if(ch>127){q.set_error(ERROR_MOD_NOT_FOUND);return 0;}name+=char(ch);}
        return q.module(name);
    });
    // One fiber per logical guest thread. Compiled destructors run on its CPU.
    p.register_api("kernel32.dll","FlsAlloc",1,[state](Process& q)->U32 {
        const auto callback=q.argument(0);if(callback)q.memory.check(callback,1,Memory::Execute);
        for(U32 index=0;index<128;++index)if(!state->fiber_slots.contains(index)){state->fiber_slots[index]={{},callback};return index;}
        q.set_error(ERROR_NOT_ENOUGH_MEMORY);return 0xffffffffu;
    });
    p.register_api("kernel32.dll","FlsSetValue",2,[state](Process& q)->U32 {
        auto it=state->fiber_slots.find(q.argument(0));if(it==state->fiber_slots.end()){q.set_error(ERROR_INVALID_PARAMETER);return 0;}
        it->second.values[q.state().current_thread_id]=q.argument(1);return 1;
    });
    p.register_api("kernel32.dll","FlsGetValue",1,[state](Process& q)->U32 {
        auto it=state->fiber_slots.find(q.argument(0));if(it==state->fiber_slots.end()){q.set_error(ERROR_INVALID_PARAMETER);return 0;}
        q.set_error(ERROR_SUCCESS);return it->second.values[q.state().current_thread_id];
    });
    p.register_api("kernel32.dll","FlsFree",1,[state](Process& q)->U32 {
        const auto index=q.argument(0);auto it=state->fiber_slots.find(index);
        if(it==state->fiber_slots.end()){q.set_error(ERROR_INVALID_PARAMETER);return 0;}
        const auto slot=it->second;it->second.values.clear();
        if(slot.callback)for(const auto& [thread,value]:slot.values)if(value){const std::array<U32,1> args{value};q.callback(slot.callback,args);}
        state->fiber_slots.erase(index);return 1;
    });
    p.register_api("kernel32.dll","EncodePointer",1,[state](Process& q)->U32 {return std::rotr(q.argument(0)^state->pointer_cookie,int(state->pointer_cookie&31u));});
    p.register_api("kernel32.dll","DecodePointer",1,[state](Process& q)->U32 {return std::rotl(q.argument(0),int(state->pointer_cookie&31u))^state->pointer_cookie;});
    p.register_api("kernel32.dll","InitializeCriticalSectionAndSpinCount",2,[](Process& q)->U32 {
        const auto at=q.argument(0),spin=q.argument(1);
        if(q.state().critical_sections.contains(at))throw GuestFault(FaultKind::unsupported,q.cpu.eip,"critical section initialized twice");
        q.memory.check(at,24,Memory::Write);for(unsigned i=0;i<24;i+=4)q.memory.store(at+i,0,32);
        q.memory.store(at+4,0xffffffffu,32);q.memory.store(at+20,spin,32);
        q.state().critical_sections.insert(at);q.state().critical_depth[at]=0;return 1;
    });
    p.register_api("kernel32.dll","GetSystemTimeAsFileTime",1,[](Process& q)->U32 {
        const auto at=q.argument(0);q.memory.check(at,8,Memory::Write);
        store_u64(q,at,wa2::platform::Clock::file_time());return 0;
    });
    p.register_api("kernel32.dll","QueryPerformanceCounter",1,[](Process& q)->U32 {
        const auto at=q.argument(0);q.memory.check(at,8,Memory::Write);
        store_u64(q,at,wa2::platform::Clock::counter());return 1;
    });
    p.register_api("kernel32.dll","QueryPerformanceFrequency",1,[](Process& q)->U32 {
        const auto at=q.argument(0);q.memory.check(at,8,Memory::Write);
        store_u64(q,at,wa2::platform::Clock::frequency);return 1;
    });
    p.state().thread_exit.push_back([state,&p](U32 thread){
        for(unsigned pass=0;pass<4;++pass){std::vector<std::array<U32,2>> pending;for(auto& [index,slot]:state->fiber_slots){auto it=slot.values.find(thread);if(it!=slot.values.end()){const auto value=it->second;slot.values.erase(it);if(value && slot.callback)pending.push_back({slot.callback,value});}}if(pending.empty())return;for(const auto& call:pending){const std::array<U32,1> args{call[1]};p.callback(call[0],args);}}
        throw GuestFault(FaultKind::unsupported,p.cpu.eip,"FLS destructor iteration limit");
    });
    p.state().reports["windows_abi"]=[]{return "{\"host\":\"Windows x64\",\"native_pointer_bits\":64,\"native_handle_bits\":64,\"native_message_parameter_bits\":64,\"guest_serialized_pointer_bits\":32,\"requires_wow64\":false}";};
    p.state().reports["wa2_cpu_features"]=[&p]{try{return std::string("{\"sse_math\":")+std::to_string(p.memory.load(0xbc830c,32))+",\"sse_ftol\":"+std::to_string(p.memory.load(0xbc8310,32))+"}";}catch(const GuestFault&){return std::string("null");}};
    install_wa2_kernel64(p);install_wa2_file_dialogs(p);install_wa2_exceptions(p);install_wa2_threads(p);install_wa2_fast_paths(p);
    install_wa2_native_resources(p);
    install_wa2_native_vorbis(p);
#if defined(WA2_CPP_BLOCK_BACKEND)
    for(const auto& entry:wa2_block_entries())p.state().fast_entries.at(entry.pc-p.state().fast_base)|=2;
    p.state().block_step=wa2_block_batch;
#endif
    p.state().reports["static_blocks"]=[&p]{return std::string("{\"batches\":")+std::to_string(p.state().block_batches)+",\"guest_instructions\":"+std::to_string(p.state().block_instructions)+"}";};
}
}
