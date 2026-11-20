#pragma once
#include <chrono>
#include <cstdint>

namespace wa2::aot {
// Measure only a complete source ten-slot preview loop ending at a real
// window presentation. A partial loop cannot extend into the next frame.
struct SavePageTiming {
    std::uint32_t first{}, caller{}, slots{};
    std::uint64_t start_ns{}, pages{}, last_ns{};

    void visit(std::uint32_t slot,std::uint32_t source,std::uint64_t now){
        if(slots && caller==source && slot==first+slots-1)return;
        if(slot%10==0){
            first=slot;caller=source;slots=1;start_ns=now;
        }else if(slots && caller==source && slot==first+slots){
            ++slots;
        }else slots=0;
    }
    void presented(std::uint64_t now){
        if(slots==10 && now>=start_ns){++pages;last_ns=now-start_ns;}
        slots=0;
    }
};
inline thread_local SavePageTiming save_page_timing;
inline std::uint64_t save_page_now(){
    using namespace std::chrono;
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}
}
