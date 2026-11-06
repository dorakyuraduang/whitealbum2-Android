#include "timeline.hpp"
#include <stdexcept>
#include <limits>

namespace wa2::game {
std::int64_t Timeline::sample(std::int64_t now){
    if(now<0 || (initialized_ && now<observed_))throw std::invalid_argument("Gameplay monotonic clock moved backwards");
    if(!initialized_){logical_=now;initialized_=true;}
    else if(!paused_){const auto delta=now-observed_;if(delta>std::numeric_limits<std::int64_t>::max()-logical_)throw std::overflow_error("Gameplay logical clock exhausted");logical_+=delta;}
    observed_=now;return logical_;
}
bool Timeline::pause(bool value,std::int64_t now){sample(now);const bool changed=value!=paused_;paused_=value;return changed;}
void Timeline::rebase(std::int64_t logical,std::int64_t now){if(logical<0 || now<0)throw std::invalid_argument("Invalid restored gameplay clock");logical_=logical;observed_=now;initialized_=true;paused_=false;}
}
