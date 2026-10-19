#include "scene_controls.hpp"
#include <bit>

namespace wa2::script {
SceneControls::SceneControls(Milliseconds clock,std::function<bool()> interrupt_timer,std::function<bool()> interrupt_wait,std::function<bool()> skipping)
    :clock_(std::move(clock)),interrupt_timer_(std::move(interrupt_timer)),interrupt_wait_(std::move(interrupt_wait)),skipping_(std::move(skipping)){
    if(!clock_)throw Error("missing native scene clock");
}
std::optional<CommandResult> SceneControls::dispatch(Command& command){
    auto count=[&](std::size_t expected){if(command.arguments.size()!=expected)throw Error("invalid native scene control parameters");};
    const auto integer=[&](std::size_t index){return command.arguments[index].integer();};
    const auto result=[](std::int32_t value=0){return CommandResult{true,false,Value(value)};};
    switch(command.index){
    case 18:count(0);return result(std::bit_cast<std::int32_t>(clock_()));
    case 135:count(0);return CommandResult{(interrupt_wait_ && interrupt_wait_()) || (skipping_ && skipping_()),true,Value(1)};
    // VI ignores its first (generic) argument. -1 retains both existing fields.
    // Existing complete functions: 0x454d90 -> 0x406140.
    case 137:count(2);if(integer(1)!=-1){modes_.voice_chapter=integer(1);modes_.voice_offset=0;}return result();
    case 170:count(1);modes_.time=integer(0);return result();
    case 171:{
        count(2);const auto selector=integer(1),value=integer(0);
        if(selector==0 || selector==1)modes_.background_chroma=value;
        if(selector==0 || selector==2)modes_.character_chroma=value;
        return result();
    }
    case 172:{
        count(2);const auto selector=integer(1);const auto& value=command.arguments[0].text();
        if(selector==0 || selector==1)modes_.background_effect=value;
        if(selector==0 || selector==2)modes_.character_effect=value;
        return result();
    }
    case 194:{
        count(3);bool complete=false;
        if(!wait_counter_){wait_counter_=1;wait_value_=std::uint32_t(integer(0))+(integer(1)?clock_():0);}
        else{
            ++wait_counter_;
            complete=integer(1)?wait_value_<=clock_():std::bit_cast<std::int32_t>(wait_value_)<=std::bit_cast<std::int32_t>(wait_counter_);
        }
        if(integer(2)==0 && interrupt_wait_ && interrupt_wait_())complete=true;
        if(complete)wait_counter_=0;
        return CommandResult{complete,true,Value{}};
    }
    case 195:count(0);timer_origin_=clock_();return result();
    case 196:{
        count(1);const auto deadline=timer_origin_+std::uint32_t(integer(0));
        const bool waiting=clock_()<deadline && !(interrupt_timer_ && interrupt_timer_());
        return CommandResult{!waiting,true,{}};
    }
    // LogOut is a state setter (0x455c80 -> 0x4033a0), not a wait/window
    // command. The dialogue/backlog consumer reads this original flag.
    case 200:count(1);modes_.log_output=integer(0);return result();
    case 204:count(0);return result(std::bit_cast<std::int32_t>(clock_()-timer_origin_));
    case 205:count(0);return result(skipping_ && skipping_());
    case 206:count(0);return result((skipping_ && skipping_()) || (interrupt_wait_ && interrupt_wait_()));
    // Existing 0x455db0 sets the redraw flag and returns 1 to yield a frame.
    case 207:count(0);modes_.draw_requested=true;return CommandResult{true,true,Value(1)};
    default:return std::nullopt;
    }
}
}
