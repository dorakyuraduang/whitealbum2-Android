#include "scene_colour.hpp"
#include "checkpoint_codec.hpp"
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
std::vector<std::uint8_t> SceneColour::checkpoint()const{
    checkpoint::Writer out;for(const auto& channels:{current_,from_,target_})for(auto value:channels)out.i32(value);
    out.i32(duration_);out.i32(second_duration_);out.i32(elapsed_);out.boolean(active_);out.boolean(returning_);return out.seal("LSCO");
}
void SceneColour::restore(std::span<const std::uint8_t> bytes){
    auto in=checkpoint::Reader::open("LSCO",bytes);SceneColour next;
    for(auto* channels:{&next.current_,&next.from_,&next.target_})for(auto& value:*channels){value=in.i32();if(value<0 || value>255)throw Error("Invalid saved scene colour");}
    next.duration_=in.i32();next.second_duration_=in.i32();next.elapsed_=in.i32();next.active_=in.boolean();next.returning_=in.boolean();in.finish();
    if(next.elapsed_<0 || (next.returning_ && !next.active_))throw Error("Invalid saved scene colour fade");*this=next;
}
}
