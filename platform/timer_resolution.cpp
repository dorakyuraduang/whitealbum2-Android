#include "clock.hpp"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#endif
namespace wa2::platform {
TimerCaps TimerResolution::caps() noexcept {
#ifdef _WIN32
    TIMECAPS caps{};if(timeGetDevCaps(&caps,sizeof(caps))!=TIMERR_NOERROR)return {};
    return {caps.wPeriodMin,caps.wPeriodMax,true};
#else
    return {1,1000000,true};
#endif
}
bool TimerResolution::begin(std::uint32_t period) {
    const auto limits=caps();if(!limits.valid || period<limits.minimum || period>limits.maximum)return false;
    auto [found,inserted]=periods_.try_emplace(period,0);
#ifdef _WIN32
    if(timeBeginPeriod(period)!=TIMERR_NOERROR){if(inserted)periods_.erase(found);return false;}
#endif
    ++found->second;return true;
}
bool TimerResolution::end(std::uint32_t period) noexcept {
    auto found=periods_.find(period);if(found==periods_.end())return false;
#ifdef _WIN32
    if(timeEndPeriod(period)!=TIMERR_NOERROR)return false;
#endif
    if(!--found->second)periods_.erase(found);return true;
}
TimerResolution::~TimerResolution() {
#ifdef _WIN32
    for(const auto& [period,count]:periods_)for(unsigned n=0;n<count;++n)timeEndPeriod(period);
#endif
}
}
