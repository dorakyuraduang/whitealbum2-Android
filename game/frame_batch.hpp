#pragma once
#include "script_runtime.hpp"
#include <cstdint>
#include <stdexcept>

namespace wa2::game {
struct FrameBudget {std::int64_t microseconds{2000};std::uint32_t maximum_ticks{256};};
enum class FrameLimit {none,time,ticks};
struct FrameTick {script::RunState state;bool accelerate{};};
struct FrameBatch {script::RunState state;std::uint32_t ticks{};FrameLimit limit{FrameLimit::none};};
// Shared scheduling policy, independent of an OS clock or presentation backend.
// A step reports whether further skip work is useful; resource waits/choices
// end the batch immediately. The first tick alone consumes transient input.
template<class Step,class Clock>FrameBatch run_frame_batch(Step&& step,Clock&& clock,FrameBudget budget={}){
    if(budget.microseconds<0 || budget.microseconds>50000 || !budget.maximum_ticks || budget.maximum_ticks>4096)
        throw std::invalid_argument("Native frame budget is out of range");
    const std::int64_t start=clock();if(start<0)throw std::invalid_argument("Native frame budget clock is negative");auto previous=start;
    auto tick=step(true);FrameBatch batch{tick.state,1};
    while(tick.accelerate && tick.state==script::RunState::yielded){
        if(batch.ticks>=budget.maximum_ticks){batch.limit=FrameLimit::ticks;break;}
        const std::int64_t now=clock();if(now<previous)throw std::invalid_argument("Native frame budget clock moved backwards");previous=now;
        if(now-start>=budget.microseconds){batch.limit=FrameLimit::time;break;}
        tick=step(false);batch.state=tick.state;++batch.ticks;
    }
    return batch;
}
}
