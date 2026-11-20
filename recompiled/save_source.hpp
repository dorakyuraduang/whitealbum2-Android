#pragma once
#include "save_page_timing.hpp"
#include "save_thumbnail.hpp"
#include <winrecomp/process.hpp>
#include <winrecomp/win32_state.hpp>
#include <array>
#include <map>
#include <sstream>
#include <tuple>

namespace wa2::aot {
// Only address/frame layouts differ. Admission, batching and timing are shared.
struct SaveReadSource {
    wr::U32 screen,update;
    std::array<wr::U32,3> helpers;
    wr::U32 api_frame,index,preview,buffer,preview_return,preview_slot;
    std::array<wr::U32,2> pages;
};
inline constexpr SaveReadSource main_save_source{0xa39208,0xa3a130,
    {0x44c4df,0x44c53f,0x44c60d},40,0x423465,0x42357e,0xa4e5c0,0x154,0x158,{0x41abe9,0x41d9ba}};
struct SaveReadAudit {
    std::uint64_t calls{},admitted{},faults{},overflow{};
    std::map<std::tuple<wr::U32,wr::U32,wr::U32,wr::U32>,std::uint64_t> contexts;
    void observe(wr::U32 thread,wr::U32 caller,wr::U32 screen,wr::U32 writers)noexcept{try{
        ++calls;const auto key=std::make_tuple(thread,caller,screen,writers);auto found=contexts.find(key);
        if(found!=contexts.end())++found->second;else if(contexts.size()<32)contexts.emplace(key,1);else ++overflow;
    }catch(...){++overflow;}}
    std::string report()const{
        std::ostringstream out;out<<"{\"calls\":"<<calls<<",\"admitted\":"<<admitted<<",\"memory_faults\":"<<faults<<",\"overflow\":"<<overflow
            <<",\"completed_preview_pages\":"<<save_page_timing.pages<<",\"last_preview_to_present_ns\":"<<save_page_timing.last_ns
            <<",\"thumbnail_copies\":"<<save_thumbnail_counts.copies<<",\"thumbnail_bytes\":"<<save_thumbnail_counts.bytes<<",\"contexts\":[";bool first=true;
        for(const auto& [key,count]:contexts){if(!first)out<<',';first=false;const auto [thread,caller,screen,writers]=key;out<<"{\"thread\":"<<thread<<",\"caller\":"<<caller<<",\"screen_state\":"<<screen<<",\"open_writers\":"<<writers<<",\"calls\":"<<count<<'}';}
        return out.str()+"]}";
    }
};
inline std::optional<std::uint64_t> save_read_scope(wr::Process& p,const SaveReadSource& source,SaveReadAudit* audit=nullptr)noexcept{
    try{
        auto m=p.memory.execution_scope();const auto caller=m.load<32>(p.cpu.r[wr::ESP]);
        const auto screen=m.load<32>(source.screen);wr::U32 writers{};
        for(const auto& [handle,file]:p.state().files)if(file.owned && file.write && !file.path.empty())++writers;
        if(audit)audit->observe(p.state().current_thread_id,caller,screen,writers);
        if(p.state().current_thread_id!=1 || writers || std::find(source.helpers.begin(),source.helpers.end(),caller)==source.helpers.end())return {};
        if(audit)++audit->admitted;
        return (std::uint64_t(m.load<32>(source.update))<<32)|screen;
    }catch(const wr::GuestFault&){if(audit)++audit->faults;return {};}
}
inline std::function<std::optional<std::uint64_t>()> traced_save_read_scope(wr::Process& p,SaveReadSource source){
    save_page_timing={};save_thumbnail_counts={};
    auto audit=std::make_shared<SaveReadAudit>();p.state().reports["save_read_scope"]=[audit]{return audit->report();};
    return [&p,source,audit]{return save_read_scope(p,source,audit.get());};
}
inline bool save_list_reader(wr::Process& p,const SaveReadSource& source,bool index_only)noexcept{
    try{
        auto m=p.memory.execution_scope();const auto sp=p.cpu.r[wr::ESP],f=source.api_frame;
        if(std::uint64_t(sp)+f+16>0x100000000ull || m.load<32>(sp)!=source.helpers[0] || m.load<32>(sp+f+8)!=source.buffer)return false;
        const auto caller=m.load<32>(sp+f),bytes=m.load<32>(sp+f+12);
        return (caller==source.index && bytes==24) || (!index_only && caller==source.preview && bytes==0x10058);
    }catch(const wr::GuestFault&){return false;}
}
inline std::function<bool()> save_index_read_ahead(wr::Process& p,SaveReadSource source){return [&p,source]{return save_list_reader(p,source,true);};}
inline std::function<bool()> save_list_metadata_read(wr::Process& p,SaveReadSource source){return [&p,source]{return save_list_reader(p,source,false);};}
inline std::function<bool()> save_page_preview_read_ahead(wr::Process& p,SaveReadSource source){
    return [&p,source]()noexcept{
        try{
            auto m=p.memory.execution_scope();const auto sp=p.cpu.r[wr::ESP],f=source.api_frame;
            if(std::uint64_t(sp)+source.preview_slot+4>0x100000000ull || m.load<32>(sp)!=source.helpers[0] ||
               m.load<32>(sp+f)!=source.preview || m.load<32>(sp+f+8)!=source.buffer || m.load<32>(sp+f+12)!=0x10058)return false;
            const auto slot=m.load<32>(sp+source.preview_slot),caller=m.load<32>(sp+source.preview_return);
            if(slot>=100 || std::find(source.pages.begin(),source.pages.end(),caller)==source.pages.end())return false;
            save_page_timing.visit(slot,caller,slot%10==0?save_page_now():0);return true;
        }catch(const wr::GuestFault&){return false;}
    };
}
}
