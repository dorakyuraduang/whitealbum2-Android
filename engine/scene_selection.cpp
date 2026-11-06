#include "scene_selection.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace wa2::script {
SceneSelection::SceneSelection(Loader loader,Text text,Sound sound):loader_(std::move(loader)),text_(std::move(text)),sound_(std::move(sound)){
    if(!loader_ || !text_)throw Error("Missing native selection assets or text preparation");rows_.reserve(8);labels_.reserve(8);sprites_.reserve(1024);
}
void SceneSelection::clear(){rows_.clear();labels_.clear();requests_={};images_={};sprites_.clear();focused_.reset();elapsed_=opacity_=0;active_=ready_=needs_marker_=false;}
void SceneSelection::settings(SelectionSettings value){if(value.skin<0 || value.skin>9)throw Error("Native selection skin out of range");if(value.skin!=settings_.skin)generation_.reset();settings_=value;}
void SceneSelection::sync(const SceneChoices& choices){
    if(owner_==&choices && generation_ && *generation_==choices.generation() && active_==choices.active())return;
    clear();owner_=&choices;generation_=choices.generation();if(!choices.active())return;active_=true;
    const auto entries=choices.items();const auto visible=std::count_if(entries.begin(),entries.end(),[](const auto& row){return row.visible;});
    if(!visible || visible>8)throw Error("Native selection has invalid visible row count");
    unsigned position{};for(unsigned n=0;n<entries.size();++n){const auto& row=entries[n];if(!row.visible)continue;
        const auto chosen=choices.previously_chosen(n);rows_.push_back({n,float(334+int(position)*84-int(visible)*42),row.enabled,chosen});
        labels_.push_back(text_(row.text));needs_marker_|=chosen;++position;
    }
    std::array<char,32> name{};std::snprintf(name.data(),name.size(),"sys_%01d0200.tga",int(settings_.skin));requests_[0]=loader_(name.data());
    if(!requests_[0])throw Error("Native selection background request is missing");
    if(needs_marker_){std::snprintf(name.data(),name.size(),"sys_%01d0011.tga",int(settings_.skin));requests_[1]=loader_(name.data());if(!requests_[1])throw Error("Native selection history marker request is missing");}
}
void SceneSelection::update(const std::function<std::int32_t(std::int32_t)>& duration){
    if(!active_)return;
    for(unsigned n=0;n<images_.size();++n)if(requests_[n])if(auto image=requests_[n]->get()){images_[n]=std::move(image);requests_[n].reset();
        if((n==0 && (images_[n]->width<784 || images_[n]->height<216)) || (n==1 && (images_[n]->width<248 || images_[n]->height<104)))throw Error("Native selection atlas is smaller than its original crops");}
    if(loading() || ready_)return;
    const auto ticks=duration?duration(30):30;++elapsed_;
    if(ticks<=0 || std::int64_t(elapsed_)>=ticks){opacity_=255;ready_=true;}
    else opacity_=std::uint8_t(std::uint64_t(elapsed_)*255/std::uint32_t(ticks));
}
std::optional<std::uint32_t> SceneSelection::input(SelectionInput input){
    if(!std::isfinite(input.x) || !std::isfinite(input.y) || input.navigation< -1 || input.navigation>1)throw Error("Native selection input out of range");
    if(!ready())return {};
    const auto old_focus=focused_;
    if(input.pointer){focused_.reset();for(const auto& row:rows_)if(input.x>=358 && input.x<918 && input.y>=row.y && input.y<row.y+40){focused_=row.ordinal;break;}}
    if(input.navigation){auto it=std::find_if(rows_.begin(),rows_.end(),[&](const auto& row){return focused_ && row.ordinal==*focused_;});
        auto index=it==rows_.end()?-1:int(it-rows_.begin());
        index=index<0?0:(index+input.navigation+int(rows_.size()))%int(rows_.size());focused_=rows_[std::size_t(index)].ordinal;}
    if(focused_){
        const auto row=std::find_if(rows_.begin(),rows_.end(),[&](const auto& item){return item.ordinal==*focused_;});
        const bool available=row->enabled && (!settings_.replay || row->chosen);
        if(available && old_focus!=focused_ && sound_)sound_(SelectionSound::focus);
        if(input.confirm || (input.pointer && input.activate)){if(sound_)sound_(available?SelectionSound::accept:SelectionSound::reject);if(available)return focused_;}
    }
    return {};
}
std::span<const image::Sprite> SceneSelection::sprites(){
    sprites_.clear();if(!active_ || loading() || !opacity_)return sprites_;
    for(const auto& row:rows_){const auto append=[&](float source_y,image::SpriteBlend blend){sprites_.push_back({images_[0],248,row.y-14,784,72,0,source_y,784,72,255,255,255,opacity_});
            auto& sprite=sprites_.back();sprite.blend=blend;sprite.layer=0x1057;sprite.order=int(row.ordinal)*4;};
        append(144,image::SpriteBlend::add);append(0,image::SpriteBlend::alpha);
        if(focused_ && *focused_==row.ordinal && row.enabled && (!settings_.replay || row.chosen))append(72,image::SpriteBlend::add);
    }
    for(const auto& row:rows_)if(row.chosen){sprites_.push_back({images_[1],328,row.y,40,40,208,64,40,40,255,255,255,opacity_});sprites_.back().layer=0x1058;sprites_.back().order=int(row.ordinal)*4+3;}
    for(unsigned n=0;n<rows_.size();++n){const auto& row=rows_[n];const auto colour=!row.enabled || (settings_.replay && !row.chosen)?settings_.disabled:focused_ && *focused_==row.ordinal?settings_.highlight:settings_.normal;
        const auto first=sprites_.size();labels_[n].append(sprites_,0,std::numeric_limits<std::uint32_t>::max()/16,358,row.y,opacity_,colour);
        for(auto at=first;at<sprites_.size();++at){sprites_[at].layer=0x1059;sprites_[at].order=int(row.ordinal);}}
    return sprites_;
}
}
