#include "scene_novel.hpp"
#include "checkpoint_codec.hpp"

namespace wa2::script {
void SceneNovel::alpha(std::int32_t value){
    if(value<0 || value>255)throw Error("Native novel opacity out of range");
    alpha_=std::uint8_t(value);if(!active_)opacity_=hidden_?0:alpha_;
}
std::optional<CommandResult> SceneNovel::dispatch(Command& command){
    if(command.index!=220 && command.index!=233 && command.index!=234)return {};
    if(command.arguments.size()!=(command.index==220?1u:2u))throw Error("Invalid native novel parameters");
    if(command.index==220){
        enabled_=command.arguments[0].integer()!=0;hidden_=false;text_visible_=true;active_=false;elapsed_=0;opacity_=alpha_;
        return CommandResult{true,false,Value{}};
    }
    const auto ticks=command.arguments[0].integer(),wait=command.arguments[1].integer();
    if(!command.continuation){
        const auto hide=command.index==233;
        if(enabled_ && hidden_!=hide){hidden_=hide;duration_=ticks;elapsed_=0;active_=true;}
        command.continuation=1;
    }
    return CommandResult{!wait || !busy(),wait!=0,Value{}};
}
void SceneNovel::update(const std::function<std::int32_t(std::int32_t)>& adjust){
    if(!active_ || !enabled_)return;
    text_visible_=!hidden_;
    const auto ticks=adjust?adjust(duration_):duration_==-1?15:duration_==-2?30:duration_;
    if(ticks<=0 || std::int64_t(elapsed_)>=ticks){opacity_=hidden_?0:alpha_;active_=false;elapsed_=0;return;}
    const auto amount=std::uint8_t(std::uint64_t(alpha_)*++elapsed_/std::uint32_t(ticks));
    opacity_=hidden_?std::uint8_t(alpha_-amount):amount;
}
std::vector<std::uint8_t> SceneNovel::checkpoint()const{
    checkpoint::Writer out;out.i32(duration_);out.u32(elapsed_);out.u32(alpha_);out.u32(opacity_);
    out.boolean(enabled_);out.boolean(hidden_);out.boolean(text_visible_);out.boolean(active_);return out.seal("LSNV");
}
void SceneNovel::restore(std::span<const std::uint8_t> bytes){
    auto in=checkpoint::Reader::open("LSNV",bytes);const auto ticks=in.i32();const auto elapsed=in.u32(),alpha=in.u32(),opacity=in.u32();
    const auto enabled=in.boolean(),hidden=in.boolean(),visible=in.boolean(),active=in.boolean();in.finish();
    if(alpha>255 || opacity>alpha || (!active && elapsed) || (active && !enabled))throw Error("Invalid saved native novel state");
    duration_=ticks;elapsed_=elapsed;alpha_=std::uint8_t(alpha);opacity_=std::uint8_t(opacity);enabled_=enabled;hidden_=hidden;text_visible_=visible;active_=active;
}
}
