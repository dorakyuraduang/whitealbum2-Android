#pragma once
#include <cstddef>

namespace wa2::graphics {
// Append into previously unused storage, then orphan on wrap. No region is
// overwritten while an earlier draw can still reference it. External uploads
// invalidate the plan; oversized packets retain the dedicated upload path.
template<std::size_t Capacity> class StreamBuffer {
    static_assert(Capacity>=16 && Capacity%16==0 && Capacity<=4*1024*1024);
    std::size_t cursor_{};
    bool valid_{};
public:
    struct Plan {std::size_t offset{},allocation{};bool direct{};};
    static constexpr std::size_t MaximumStorage=Capacity;
    void invalidate(){cursor_=0;valid_=false;}
    Plan reserve(std::size_t bytes){
        if(!bytes)return {};
        if(bytes>Capacity){invalidate();return {0,bytes,true};}
        const auto aligned=(bytes+15)&~std::size_t(15);
        Plan plan;
        if(!valid_ || aligned>Capacity-cursor_){cursor_=0;valid_=true;plan.allocation=Capacity;}
        plan.offset=cursor_;cursor_+=aligned;return plan;
    }
};
}
