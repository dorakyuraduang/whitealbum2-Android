#pragma once
#include "functions.hpp"
#include "save_source.hpp"
#include "save_thumbnail.hpp"
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
inline std::optional<std::uint64_t> profile_write_scope(wr::Process& p) noexcept {
    if(p.state().current_thread_id!=1)return {};
    for(const auto& [handle,file]:p.state().files)if(file.owned && file.write && !file.path.empty())return {};
    try{
        auto m=p.memory.execution_scope();const auto sp=p.cpu.r[wr::ESP];
        // Cached integer setter: 20 local bytes, three saved registers and
        // four API arguments. Only the source main's final settings sequence.
        if(std::uint64_t(sp)+68>0x100000000ull || m.load<32>(sp)!=0x44d4ba)return {};
        const auto caller=m.load<32>(sp+52);const auto location=locate(caller);
        if(caller<0x4338d9 || caller>0x433dbc || !location.function || location.function->entry!=0x432a50 ||
           m.load<32>(sp+4)!=m.load<32>(sp+64) || m.load<32>(sp+8)!=m.load<32>(sp+56))return {};
        return m.load<32>(0xa3a130);
    }catch(const wr::GuestFault&){return {};}
}
// Main address ABI; the policy implementation is shared with Special.
inline std::optional<std::uint64_t> save_read_scope(wr::Process& p,SaveReadAudit* audit=nullptr)noexcept{return save_read_scope(p,main_save_source,audit);}
inline auto traced_save_read_scope(wr::Process& p){return traced_save_read_scope(p,main_save_source);}
inline auto save_index_read_ahead(wr::Process& p){return save_index_read_ahead(p,main_save_source);}
inline auto save_list_metadata_read(wr::Process& p){return save_list_metadata_read(p,main_save_source);}
inline auto save_page_preview_read_ahead(wr::Process& p){return save_page_preview_read_ahead(p,main_save_source);}
}
