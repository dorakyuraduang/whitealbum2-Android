#include "scene_controls.hpp"
#include "checkpoint_codec.hpp"
#include <bit>

namespace wa2::script {
SceneControls::SceneControls(Milliseconds clock,std::function<bool()> interrupt_timer,std::function<bool()> interrupt_wait,std::function<bool()> skipping,std::function<std::int32_t()> readable)
    :clock_(std::move(clock)),interrupt_timer_(std::move(interrupt_timer)),interrupt_wait_(std::move(interrupt_wait)),skipping_(std::move(skipping)),readable_(std::move(readable)){
    if(!clock_)throw Error("missing native scene clock");
}
std::optional<CommandResult> SceneControls::dispatch(Command& command){
    auto count=[&](std::size_t expected){if(command.arguments.size()!=expected)throw Error("invalid native scene control parameters");};
    const auto integer=[&](std::size_t index){return command.arguments[index].integer();};
    const auto result=[](std::int32_t value=0){return CommandResult{true,false,Value(value)};};
    switch(command.index){
    case 18:count(0);return result(std::bit_cast<std::int32_t>(clock_()));
    case 135:count(0);return CommandResult{automatic_advance() || (!modes_.demo_mode && ((interrupt_wait_ && interrupt_wait_()) || (skipping_ && skipping_()))),true,Value(1)};
    case 136:count(2);modes_.demo_mode=integer(0);modes_.demo_ticks=integer(1);return result();
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
    case 194:case 236:{
        count(command.index==194?3:4);bool complete=false;
        if(!wait_counter_){wait_counter_=1;wait_value_=std::uint32_t(integer(0))+(integer(1)?clock_():0);}
        else{
            ++wait_counter_;
            complete=integer(1)?wait_value_<=clock_():std::bit_cast<std::int32_t>(wait_value_)<=std::bit_cast<std::int32_t>(wait_counter_);
        }
        if(!complete && integer(2)==0){
            if(!modes_.demo_mode && skipping_ && skipping_())complete=true;
            if(!modes_.demo_mode && command.index==236 && integer(3)==1 && interrupt_timer_ && interrupt_timer_()){
                if(!readable_)throw Error("Wait2 requires native reading-state policy");complete=readable_()!=0;
            }
            if(modes_.demo_mode==1 && interrupt_wait_ && interrupt_wait_()){
                if(!readable_)throw Error("Demo wait requires native reading-state policy");complete=readable_()!=0;
            }
        }
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
    case 219:count(0);modes_.latched_skip=false;modes_.auto_mode=false;return result();
    default:return std::nullopt;
    }
}
bool SceneControls::automatic_advance(){
    if(!modes_.demo_mode && !modes_.auto_mode)return false;
    const auto limit=modes_.demo_mode?modes_.demo_ticks:modes_.auto_ticks;
    if(std::bit_cast<std::int32_t>(message_elapsed_)>=limit){message_elapsed_=0;return true;}
    ++message_elapsed_;
    if(interrupt_wait_ && interrupt_wait_() && readable_ && readable_()!=0){message_elapsed_=0;return true;}
    return false;
}
std::vector<std::uint8_t> SceneControls::checkpoint()const{
    checkpoint::Writer out;
    for(auto v:{modes_.time,modes_.background_chroma,modes_.character_chroma,modes_.log_output,modes_.voice_chapter,modes_.voice_offset,modes_.demo_mode,modes_.demo_ticks,modes_.auto_ticks})out.i32(v);
    out.string(modes_.background_effect);out.string(modes_.character_effect);out.boolean(modes_.draw_requested);out.boolean(modes_.auto_mode);out.boolean(modes_.latched_skip);
    out.u32(timer_origin_);out.u32(wait_counter_);out.u32(wait_value_);out.u32(message_elapsed_);return out.seal("LSCT");
}
void SceneControls::restore(std::span<const std::uint8_t> bytes){
    auto in=checkpoint::Reader::open("LSCT",bytes);SceneModes next;
    next.time=in.i32();next.background_chroma=in.i32();next.character_chroma=in.i32();next.log_output=in.i32();next.voice_chapter=in.i32();next.voice_offset=in.i32();next.demo_mode=in.i32();next.demo_ticks=in.i32();next.auto_ticks=in.i32();
    next.background_effect=in.string(4096);next.character_effect=in.string(4096);next.draw_requested=in.boolean();next.auto_mode=in.boolean();next.latched_skip=in.boolean();
    const auto timer=in.u32(),counter=in.u32(),wait=in.u32(),elapsed=in.u32();in.finish();modes_=std::move(next);timer_origin_=timer;wait_counter_=counter;wait_value_=wait;message_elapsed_=elapsed;
}
}
