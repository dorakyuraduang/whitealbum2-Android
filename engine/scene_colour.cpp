#include "scene_colour.hpp"
#include <algorithm>

namespace wa2::script {
std::optional<CommandResult> SceneColour::dispatch(Command& command){
    if(command.index!=152 && command.index!=153)return std::nullopt;
    const bool flash=command.index==152;
    if(command.arguments.size()!=(flash?5:4))throw Error("invalid native background colour parameters");
    if(!command.continuation){
        duration_=command.arguments[0].integer();second_duration_=flash?command.arguments[1].integer():0;
        for(std::size_t n=0;n<3;++n)target_[n]=std::clamp(command.arguments[(flash?2:1)+n].integer(),0,255);
        from_=current_;active_=true;returning_=flash;elapsed_=0;command.continuation=1;
    }
    return CommandResult{!active_,true,Value(1)};
}
void SceneColour::update(const std::function<std::int32_t(std::int32_t)>& adjust){
    if(!active_)return;
    const auto duration=adjust?adjust(duration_):duration_;
    if(elapsed_<duration){
        ++elapsed_;
        for(std::size_t n=0;n<3;++n)current_[n]=std::int32_t(std::int64_t(duration-elapsed_)*from_[n]/duration+
            std::int64_t(elapsed_)*target_[n]/duration);
    }else{
        elapsed_=0;current_=from_=target_;
        if(returning_){returning_=false;duration_=second_duration_;target_={128,128,128};}
        else active_=false;
    }
}
}
