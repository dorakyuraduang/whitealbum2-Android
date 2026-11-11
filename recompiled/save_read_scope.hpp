#pragma once
#include "functions.hpp"
#include <winrecomp/process.hpp>
#include <winrecomp/win32_state.hpp>
#include <optional>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <tuple>

namespace wa2::aot {
inline std::optional<std::uint64_t> profile_read_scope(wr::Process& p) noexcept {
    if(p.state().current_thread_id!=1)return {};
    for(const auto& [handle,file]:p.state().files)
        if(file.owned && file.write && !file.path.empty())return {};
    try{
        auto m=p.memory.execution_scope();
        if(!locate(m.load<32>(p.cpu.r[wr::ESP])).function)return {};
        return m.load<32>(0xa3a130);
    }catch(const wr::GuestFault&){return {};}
}
// The source enumerates save slots by opening each filename, including absent
// slots. Share one directory inventory within that source update.
struct SaveReadAudit {
    std::uint64_t calls{},admitted{},faults{},overflow{};
    std::map<std::tuple<wr::U32,wr::U32,wr::U32,wr::U32>,std::uint64_t> contexts;
    void observe(wr::U32 thread,wr::U32 caller,wr::U32 screen,wr::U32 writers)noexcept{try{
        ++calls;const auto key=std::make_tuple(thread,caller,screen,writers);auto found=contexts.find(key);
        if(found!=contexts.end())++found->second;else if(contexts.size()<32)contexts.emplace(key,1);else ++overflow;
    }catch(...){++overflow;}}
    std::string report()const{
        std::ostringstream out;out<<"{\"calls\":"<<calls<<",\"admitted\":"<<admitted<<",\"memory_faults\":"<<faults<<",\"overflow\":"<<overflow<<",\"contexts\":[";bool first=true;
        for(const auto& [key,count]:contexts){if(!first)out<<',';first=false;const auto [thread,caller,screen,writers]=key;out<<"{\"thread\":"<<thread<<",\"caller\":"<<caller<<",\"screen_state\":"<<screen<<",\"open_writers\":"<<writers<<",\"calls\":"<<count<<'}';}
        return out.str()+"]}";
    }
};
// Only the known read-only source helpers qualify, including title-menu
// loads. Story substates are not an admission requirement. Ordinary API
// callers still read fresh; payloads and descriptors are never cached.
inline std::optional<std::uint64_t> save_read_scope(wr::Process& p,SaveReadAudit* audit=nullptr) noexcept {
    try{
        auto m=p.memory.execution_scope();const auto caller=m.load<32>(p.cpu.r[wr::ESP]);
        const auto screen=m.load<32>(0xa39208);wr::U32 writers{};
        for(const auto& [handle,file]:p.state().files)if(file.owned && file.write && !file.path.empty())++writers;
        if(audit)audit->observe(p.state().current_thread_id,caller,screen,writers);
        if(p.state().current_thread_id!=1 || writers)return {};
        if(caller!=0x44c4df && caller!=0x44c53f && caller!=0x44c60d)return {};
        // 0x418f8a increments this cumulative counter after each update.
        // Unlike 0xa3a378, it is not the one-second FPS counter.
        if(audit)++audit->admitted;
        return (std::uint64_t(m.load<32>(0xa3a130))<<32)|screen;
    }catch(const wr::GuestFault&){if(audit)++audit->faults;return {};}
}
inline std::function<std::optional<std::uint64_t>()> traced_save_read_scope(wr::Process& p){
    const auto audit=std::make_shared<SaveReadAudit>();p.state().reports["save_read_scope"]=[audit]{return audit->report();};
    return [&p,audit]{return save_read_scope(p,audit.get());};
}
// Only the 24-byte index helper scans the whole bank. Preview/full-load calls
// must not open neighbouring slots or consume previously prepared streams.
inline std::function<bool()> save_index_read_ahead(wr::Process& p){
    return [&p]() noexcept {
        try{
            auto m=p.memory.execution_scope();const auto sp=p.cpu.r[wr::ESP];
            if(std::uint64_t(sp)+56>0x100000000ull)return false;
            return m.load<32>(sp)==0x44c4df && m.load<32>(sp+40)==0x423465 &&
                m.load<32>(sp+48)==0xa4e5c0 && m.load<32>(sp+52)==24;
        }catch(const wr::GuestFault&){return false;}
    };
}
}
