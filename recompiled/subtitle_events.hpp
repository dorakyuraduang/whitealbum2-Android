#pragma once
#include <winrecomp/runtime.hpp>

namespace wa2::aot {
// Only the five proven audio function returns emit events. Ordinary functions
// incur no hook lookup, instruction dispatch or additional source-memory work.
struct SubtitleEvents {
    wr::Memory* memory{};
    void (*notify)(std::uint32_t,const wr::Cpu&,wr::Memory&,std::uint32_t,void*){};
    void* owner{};
};
inline thread_local SubtitleEvents* current_subtitle_events{};
class SubtitleEventScope {
    SubtitleEvents events_;
    SubtitleEvents* previous_{current_subtitle_events};
public:
    SubtitleEventScope(wr::Memory& memory,decltype(SubtitleEvents::notify) notify,void* owner):events_{&memory,notify,owner}{current_subtitle_events=&events_;}
    ~SubtitleEventScope(){current_subtitle_events=previous_;}
    SubtitleEventScope(const SubtitleEventScope&)=delete;
};
template<bool Metered>inline void notify_subtitle_return(std::uint32_t entry,const wr::Cpu& cpu,wr::Memory& memory,std::uint32_t arguments){
    if constexpr(!Metered)if(auto* events=current_subtitle_events;events && events->memory==&memory && events->notify)
        events->notify(entry,cpu,memory,arguments,events->owner);
}
}
