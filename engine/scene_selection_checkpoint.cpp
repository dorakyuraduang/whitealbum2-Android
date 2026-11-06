#include "scene_selection.hpp"
#include "resource_catalog.hpp"
#include "checkpoint_codec.hpp"
#include <algorithm>
#include <cstdio>

namespace wa2::script {
std::vector<std::uint8_t> SceneSelection::checkpoint(const SceneChoices& choices,const resource::Catalog& resources)const{
    if(active_!=choices.active() || (active_ && (owner_!=&choices || !generation_ || *generation_!=choices.generation())))throw Error("Native selection must be synchronized before saving");
    checkpoint::Writer out;out.blob(choices.checkpoint());out.i32(settings_.skin);out.boolean(settings_.replay);for(auto colour:{settings_.normal,settings_.highlight,settings_.disabled}){out.u32(colour.r);out.u32(colour.g);out.u32(colour.b);}
    out.boolean(active_);out.boolean(ready_);out.boolean(needs_marker_);out.u32(elapsed_);out.u32(opacity_);out.boolean(focused_.has_value());if(focused_)out.u32(*focused_);
    for(unsigned n=0;n<2;++n){out.boolean(bool(requests_[n]));if(requests_[n])out.blob(image::checkpoint_reference(resources.reference(requests_[n])));out.boolean(bool(images_[n]));if(images_[n])out.blob(image::checkpoint_image(resources,images_[n]));}
    return out.seal("LSSE");
}
void SceneSelection::restore(std::span<const std::uint8_t> bytes,const SceneChoices& choices,resource::Catalog& resources,const image::Resolve& resolve){
    if(!resolve)throw Error("Missing saved selection image resolver");auto in=checkpoint::Reader::open("LSSE",bytes);const auto signature=in.blob(16384);const auto expected=choices.checkpoint();if(signature.size()!=expected.size() || !std::equal(signature.begin(),signature.end(),expected.begin()))throw Error("Saved selection does not match its choices");
    SceneSelection next(loader_,text_,sound_);next.settings_.skin=in.i32();next.settings_.replay=in.boolean();for(auto* colour:{&next.settings_.normal,&next.settings_.highlight,&next.settings_.disabled}){const auto red=in.u32(),green=in.u32(),blue=in.u32();if(red>255 || green>255 || blue>255)throw Error("Invalid saved selection colour");*colour={std::uint8_t(red),std::uint8_t(green),std::uint8_t(blue)};}
    next.active_=in.boolean();next.ready_=in.boolean();const auto saved_marker=in.boolean();next.elapsed_=in.u32();const auto opacity=in.u32();if(in.boolean())next.focused_=in.u32();if(next.settings_.skin<0 || next.settings_.skin>9 || opacity>255 || next.elapsed_>1000000 || next.active_!=choices.active())throw Error("Invalid saved selection presentation");next.opacity_=std::uint8_t(opacity);
    for(unsigned n=0;n<2;++n){if(in.boolean())next.requests_[n]=resources.image(image::restore_reference(in.blob(2048)));if(in.boolean())next.images_[n]=image::restore_image(in.blob(4096),resources,resolve);if(next.requests_[n] && next.images_[n])throw Error("Saved selection image is both pending and loaded");}
    in.finish();next.owner_=&choices;next.generation_=choices.generation();
    if(!next.active_){if(next.ready_ || saved_marker || next.elapsed_ || opacity || next.focused_ || next.requests_[0] || next.requests_[1] || next.images_[0] || next.images_[1])throw Error("Inactive saved selection owns presentation state");}
    else{
        if((!next.requests_[0] && !next.images_[0]) || (saved_marker!=bool(next.requests_[1] || next.images_[1])) || (next.ready_ && (!next.images_[0] || (saved_marker && !next.images_[1]))) || (next.ready_ && opacity!=255))throw Error("Inconsistent saved selection readiness");
        const auto entries=choices.items();const auto visible=std::count_if(entries.begin(),entries.end(),[](const auto& row){return row.visible;});if(!visible || visible>8)throw Error("Invalid saved visible selection count");unsigned position{};
        for(unsigned n=0;n<entries.size();++n)if(entries[n].visible){const auto chosen=choices.previously_chosen(n);next.rows_.push_back({n,float(334+int(position)*84-int(visible)*42),entries[n].enabled,chosen});next.labels_.push_back(text_(entries[n].text));next.needs_marker_|=chosen;++position;}
        if(next.focused_ && std::none_of(next.rows_.begin(),next.rows_.end(),[&](const auto& row){return row.ordinal==*next.focused_;}))throw Error("Saved selection focus is not visible");
        // Choice history is persistent system progress, not rolled back by a
        // story load. A newly recorded choice may need a marker not in this save.
        if(next.needs_marker_ && !saved_marker){std::array<char,32> name{};std::snprintf(name.data(),name.size(),"sys_%01d0011.tga",int(next.settings_.skin));next.requests_[1]=loader_(name.data());if(!next.requests_[1])throw Error("Missing restored choice-history marker request");next.ready_=false;}
        if(!next.needs_marker_){next.requests_[1].reset();next.images_[1].reset();}
        if((next.images_[0] && (next.images_[0]->width<784 || next.images_[0]->height<216)) || (next.images_[1] && (next.images_[1]->width<248 || next.images_[1]->height<104)))throw Error("Saved selection atlas is too small");
    }
    *this=std::move(next);
}
}
