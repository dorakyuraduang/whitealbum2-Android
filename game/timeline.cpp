#include "timeline.hpp"
#include <stdexcept>

namespace wa2::game {
std::int64_t Timeline::sample(std::int64_t now){
    if(now<0 || (initialized_ && now<observed_))throw std::invalid_argument("Gameplay monotonic clock moved backwards");
    if(!initialized_){logical_=now;initialized_=true;}
    else if(!paused_)logical_+=now-observed_;
    observed_=now;return logical_;
}
bool Timeline::pause(bool value,std::int64_t now){sample(now);const bool changed=value!=paused_;paused_=value;return changed;}
}
