#include "scene_choices.hpp"
#include "checkpoint_codec.hpp"
#include <algorithm>
#include <bit>
#include <charconv>

namespace wa2::script {
SceneChoices::SceneChoices(std::shared_ptr<progress::Store> progress,std::function<std::string_view()> scene)
    :progress_(std::move(progress)),scene_(std::move(scene)){if(!scene_)throw Error("Missing native choice scene identity");}
void SceneChoices::open(std::int32_t group){
    if(!progress_)throw Error("Missing native choice progress storage");
    if(active_)throw Error("Native choice is already open");
    if(items_.empty())throw Error("Native choice has no entries");
    static constexpr std::array<std::int32_t,23> scenes={2003,2004,2005,2007,2009,2011,2013,2014,2015,2016,2017,2018,2019,2024,2503,3003,3005,3009,3012,3013,3014,3016,3904};
    const auto name=scene_();std::int32_t number{};const auto parsed=std::from_chars(name.data(),name.data()+name.size(),number);
    const auto found=parsed.ec==std::errc{}?std::find(scenes.begin(),scenes.end(),number):scenes.end();
    std::int64_t flag=992;if(found!=scenes.end())flag=900+std::int64_t(found-scenes.begin())*4+group;
    if(flag<0 || flag>=std::int64_t(progress::layout::flag_count))throw Error("Native selection-history flag out of range");
    std::size_t enabled=0;
    for(auto& item:items_){item.enabled=progress_->state().flag(item.flag)==item.expected;item.visible=item.enabled || item.show_disabled;enabled+=item.enabled;}
    if(!enabled)throw Error("Native choice has no enabled entries");
    history_flag_=std::uint32_t(flag);active_=true;result_.reset();++generation_;
}
std::optional<CommandResult> SceneChoices::dispatch(Command& command){
    if(command.index!=208 && command.index!=209)return {};
    if(command.index==208){
        if(command.arguments.size()!=4)throw Error("Invalid SetSelectMess parameters");
        if(active_ || result_)throw Error("Cannot add entries while a selection is pending");
        if(items_.size()>=8)throw Error("Native choice entry count exceeds eight");
        ChoiceItem item;item.text=command.arguments[0].text();
        if(item.text.size()>255 || item.text.find('\0')!=std::string::npos)throw Error("Native choice text exceeds original storage");
        item.flag=std::uint32_t(command.arguments[1].integer());if(item.flag>=progress::layout::flag_count)throw Error("Native choice condition flag out of range");
        item.expected=command.arguments[2].integer();item.show_disabled=command.arguments[3].integer()!=0;
        items_.push_back(std::move(item));++generation_;return CommandResult{true,false,Value{}};
    }
    if(command.arguments.size()!=1)throw Error("Invalid SetSelect parameters");
    if(!command.continuation){open(command.arguments[0].integer());command.continuation=1;}
    if(active_)return CommandResult{false,true,Value(std::int32_t(-1))};
    if(!result_)throw Error("SetSelect resumed without a selection");
    const auto selected=*result_;clear();return CommandResult{true,true,Value(selected)};
}
void SceneChoices::choose(std::uint32_t ordinal){
    if(!active_ || ordinal>=items_.size() || !items_[ordinal].enabled)throw Error("Invalid or disabled native choice selection");
    if(!progress_)throw Error("Missing native choice progress storage");
    const auto bits=std::bit_cast<std::uint32_t>(progress_->state().flag(history_flag_))|(std::uint32_t(1)<<ordinal);
    progress_->state().set_flag(history_flag_,std::bit_cast<std::int32_t>(bits));progress_->flush();
    result_=std::int32_t(ordinal);active_=false;++generation_;
}
bool SceneChoices::previously_chosen(std::uint32_t ordinal)const{
    if(ordinal>=items_.size() || !progress_)throw Error("Native choice history ordinal out of range");
    return (std::bit_cast<std::uint32_t>(progress_->state().flag(history_flag_))&(std::uint32_t(1)<<ordinal))!=0;
}
std::vector<std::uint8_t> SceneChoices::checkpoint()const{
    checkpoint::Writer out;out.boolean(active_);out.boolean(result_.has_value());if(result_)out.i32(*result_);out.u32(history_flag_);out.u32(std::uint32_t(items_.size()));
    for(const auto& item:items_){out.string(item.text);out.u32(item.flag);out.i32(item.expected);out.boolean(item.show_disabled);out.boolean(item.enabled);out.boolean(item.visible);}
    return out.seal("LSCH");
}
void SceneChoices::restore(std::span<const std::uint8_t> bytes){
    auto in=checkpoint::Reader::open("LSCH",bytes);const auto active=in.boolean();std::optional<std::int32_t> result;if(in.boolean())result=in.i32();
    const auto history=in.u32(),count=in.u32();if(history>=progress::layout::flag_count || count>8 || (active && (!count || result)))throw Error("Invalid saved selection state");
    std::vector<ChoiceItem> next;std::size_t enabled=0;
    for(unsigned n=0;n<count;++n){ChoiceItem item;item.text=in.string(255);item.flag=in.u32();item.expected=in.i32();item.show_disabled=in.boolean();item.enabled=in.boolean();item.visible=in.boolean();
        if(item.flag>=progress::layout::flag_count || item.text.find('\0')!=std::string::npos || ((active || result) && item.visible!=(item.enabled || item.show_disabled)))throw Error("Invalid saved selection entry");
        enabled+=item.enabled;next.push_back(std::move(item));
    }
    if((active && !enabled) || (result && (*result<0 || std::uint32_t(*result)>=count || !next[*result].enabled)))throw Error("Invalid saved selection result");
    in.finish();items_=std::move(next);active_=active;result_=result;history_flag_=history;++generation_;
}
}
