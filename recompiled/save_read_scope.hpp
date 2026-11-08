#pragma once
#include <winrecomp/process.hpp>
#include <winrecomp/win32_state.hpp>
#include <optional>

namespace wa2::aot {
// The source enumerates save slots by opening each filename, including absent
// slots. Share one directory inventory within that source update.
// Only known read-only helper calls in the save/load UI qualify; payloads
// and descriptors are still opened afresh through the selected provider.
inline std::optional<std::uint64_t> save_read_scope(wr::Process& p) noexcept {
    if(p.state().current_thread_id!=1)return {};
    for(const auto& [handle,file]:p.state().files)
        if(file.owned && file.write && !file.path.empty())return {};
    try{
        auto m=p.memory.execution_scope();const auto caller=m.load<32>(p.cpu.r[wr::ESP]);
        if(caller!=0x44c4df && caller!=0x44c53f && caller!=0x44c60d)return {};
        const auto screen=m.load<32>(0xa39208);
        if(screen!=6 && screen!=7)return {};
        // 0x418f8a increments this cumulative counter after each update.
        // Unlike 0xa3a378, it is not the one-second FPS counter.
        return (std::uint64_t(m.load<32>(0xa3a130))<<32)|screen;
    }catch(const wr::GuestFault&){return {};}
}
}
