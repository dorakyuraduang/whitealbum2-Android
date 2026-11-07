#include "preferences.hpp"
#include "checkpoint_codec.hpp"
#include <algorithm>
#include <stdexcept>

namespace wa2::game {
void Preferences::validate()const{
    for(auto v:{master,music,effects,speech})if(v<0 || v>256)throw std::invalid_argument("Native preference audio gain is out of range");
    if(skin<0 || skin>9)throw std::invalid_argument("Native preference window skin is out of range");
    for(auto v:{window_alpha,picture_alpha,button_alpha,novel_alpha})if(v<0 || v>256)throw std::invalid_argument("Native preference opacity is out of range");
    if(text_step<0 || text_step>1000 || auto_ticks<0 || auto_ticks>36000)throw std::invalid_argument("Native preference text timing is out of range");
    if(right_click<0 || right_click>2)throw std::invalid_argument("Native preference right-click policy is out of range");
}
float Preferences::movie_amplitude()const{validate();return float(std::min(255,effective_master()*effective_music()/256))/255.f;}
std::int32_t Preferences::transition_frames(std::int32_t frames,bool skipping)const noexcept{return skipping || !transition_wait?0:frames==-1?15:frames==-2?30:frames;}
std::vector<std::uint8_t> Preferences::encode()const{
    validate();checkpoint::Writer out;
    for(auto v:{master,music,effects,speech,skin,window_alpha,picture_alpha,button_alpha,novel_alpha,text_step,auto_ticks})out.i32(v);
    for(auto v:{window,skip_read_only,mute_adult,voice_enabled,captions})out.boolean(v);
    for(auto v:character_voice)out.boolean(v);
    out.i32(right_click);for(auto v:audio_muted)out.boolean(v);
    for(auto v:{fullscreen,confirmation,cut_voice_on_advance,transition_wait})out.boolean(v);return out.seal("WAP2");
}
Preferences Preferences::decode(std::span<const std::uint8_t> bytes){
    const auto legacy=bytes.size()>=4 && std::string_view(reinterpret_cast<const char*>(bytes.data()),4)=="WAPR";
    auto in=checkpoint::Reader::open(legacy?"WAPR":"WAP2",bytes);Preferences next;
    for(auto* field:{&next.master,&next.music,&next.effects,&next.speech,&next.skin,&next.window_alpha,&next.picture_alpha,&next.button_alpha,&next.novel_alpha,&next.text_step,&next.auto_ticks})*field=in.i32();
    for(auto* field:{&next.window,&next.skip_read_only,&next.mute_adult,&next.voice_enabled,&next.captions})*field=in.boolean();
    for(auto& v:next.character_voice)v=in.boolean();
    if(!legacy){next.right_click=in.i32();for(auto& v:next.audio_muted)v=in.boolean();for(auto* v:{&next.fullscreen,&next.confirmation,&next.cut_voice_on_advance,&next.transition_wait})*v=in.boolean();}
    in.finish();next.validate();return next;
}
PreferenceStore::PreferenceStore(std::shared_ptr<PreferenceStorage> storage):storage_(std::move(storage)){
    if(!storage_)throw std::invalid_argument("Missing native preference storage");
    if(auto bytes=storage_->read())current_=Preferences::decode(*bytes);
}
bool PreferenceStore::replace(const Preferences& next){next.validate();if(next==current_)return false;const auto bytes=next.encode();storage_->replace(bytes);current_=next;return true;}
}
