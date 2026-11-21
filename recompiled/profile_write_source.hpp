#pragma once
#include "functions.hpp"
#include <winrecomp/process.hpp>
#include <winrecomp/win32_state.hpp>
#include <optional>
#include <algorithm>
namespace wa2::aot {
struct ProfileWriteSource {
    wr::U32 api_return,caller_first,caller_last,owner,update;
    unsigned caller_slot,section_slot,key_slot;
};
inline constexpr ProfileWriteSource main_profile_write_source{0x44d4ba,0x4338d9,0x433dbc,0x432a50,0xa3a130,52,64,56};
template<class Locate>std::optional<std::uint64_t> source_profile_write_scope(wr::Process& p,const ProfileWriteSource& source,Locate locate)noexcept{
    if(p.state().current_thread_id!=1)return {};
    for(const auto& [id,file]:p.state().files)if(file.owned && file.write && !file.path.empty())return {};
    try{auto m=p.memory.execution_scope();const auto sp=p.cpu.r[wr::ESP];
        if(std::uint64_t(sp)+std::max({source.caller_slot,source.section_slot,source.key_slot})+4>0x100000000ull || m.load<32>(sp)!=source.api_return)return {};
        const auto caller=m.load<32>(sp+source.caller_slot);const auto location=locate(caller);
        if(caller<source.caller_first || caller>source.caller_last || !location.function || location.function->entry!=source.owner ||
            m.load<32>(sp+4)!=m.load<32>(sp+source.section_slot) || m.load<32>(sp+8)!=m.load<32>(sp+source.key_slot))return {};
        return m.load<32>(source.update);
    }catch(const wr::GuestFault&){return {};}
}
}
