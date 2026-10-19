#include "scene_dialogue.hpp"
#include <algorithm>
#include <cstdio>
#include <limits>

namespace wa2::script {
SceneDialogue::SceneDialogue(text::FontBook& fonts,Loader loader,std::function<void(std::int32_t)> seen,std::function<void()> end_message)
    :fonts_(fonts),loader_(std::move(loader)),seen_(std::move(seen)),end_message_(std::move(end_message)){
    if(!loader_)throw Error("missing native dialogue image loader");sprites_.reserve(512);
}
void SceneDialogue::settings(DialogueSettings value){
    if(value.skin<0 || value.skin>9 || value.window_alpha<0 || value.window_alpha>255 || value.picture_alpha<0 || value.picture_alpha>255 ||
       value.button_alpha<0 || value.button_alpha>255 || value.text_step<0 || value.text_step>1000 || value.open_ticks<0 || value.open_ticks>300)
        throw Error("invalid native dialogue settings");
    if(assets_started_ && value.skin!=settings_.skin){requests_={};images_={};assets_started_=assets_ready_=false;}
    settings_=value;
}
void SceneDialogue::load_assets(){
    if(!assets_started_){
        static constexpr std::array<unsigned,4> codes{1,0,10,11};
        for(std::size_t i=0;i<requests_.size();++i){std::array<char,32> filename{};
            std::snprintf(filename.data(),filename.size(),"sys_%01d%04u.tga",settings_.skin,codes[i]);requests_[i]=loader_(filename.data());
            if(!requests_[i])throw Error("native dialogue loader returned no request");}
        assets_started_=true;
    }
    assets_ready_=true;
    for(std::size_t i=0;i<images_.size();++i)if(!images_[i]){images_[i]=requests_[i]->get();if(images_[i])requests_[i].reset();else assets_ready_=false;}
}
void SceneDialogue::open(){
    load_assets();visible_=true;
    if(phase_==DialoguePhase::closed || phase_==DialoguePhase::closing){phase_=DialoguePhase::opening;open_elapsed_=0;}
}
void SceneDialogue::name(std::string_view value,std::int32_t speaker){
    speaker_=speaker;
    if(value==name_)return;
    old_name_text_=std::move(name_text_);name_=value;name_elapsed_=0;
    if(name_.empty()){name_text_={};return;}
    text::TextStyle style;style.columns=36;style.rows=1;style.spacing=2;style.line_gap=0;style.colour=text::speaker_colour(text::speaker_style(speaker_,name_));
    name_text_=fonts_.prepare(text::layout(name_,style));
}
void SceneDialogue::message(std::string_view value,std::int32_t id,bool replace){
    const bool append=!replace && !raw_.empty();const auto previous_pages=body_.metrics().pages.size();
    if(append)raw_+="\\k";else raw_.clear();
    raw_+=value;text::TextStyle style;style.colour=text::speaker_colour(text::speaker_style(speaker_,name_));
    body_=fonts_.prepare(text::layout(raw_,style));
    page_=append?previous_pages:0;
    // Previous portions of an appended message are already displayed. Explicit
    // \k controls in the new portion still suspend for a subsequent click.
    message_=id;budget_=idle_elapsed_=0;has_text_=true;
    if(phase_!=DialoguePhase::opening)phase_=DialoguePhase::typing;
    if(seen_)seen_(id);
}
std::uint32_t SceneDialogue::duration()const {
    if(page_>=body_.metrics().pages.size())return 0;
    return body_.metrics().pages[page_].duration+160;
}
void SceneDialogue::update(bool fast_forward){
    fast_=fast_forward;
    if(assets_started_ && !assets_ready_)load_assets();
    if(!assets_ready_ || phase_==DialoguePhase::closed)return;
    if(phase_==DialoguePhase::closing){
        if(fast_forward || open_elapsed_>=close_ticks_){phase_=DialoguePhase::closed;raw_.clear();body_={};name_text_={};old_name_text_={};name_.clear();has_text_=false;}
        else ++open_elapsed_;
        return;
    }
    if(name_elapsed_<10)++name_elapsed_;else old_name_text_={};
    if(phase_==DialoguePhase::opening){
        if(fast_forward || open_elapsed_>=std::uint32_t(settings_.open_ticks))phase_=has_text_?DialoguePhase::typing:DialoguePhase::complete;
        else ++open_elapsed_;
    }
    if(phase_==DialoguePhase::typing){
        budget_=fast_forward || !settings_.text_step?duration():std::min(duration(),budget_+std::uint32_t(settings_.text_step));
        if(budget_>=duration()){phase_=DialoguePhase::waiting;idle_elapsed_=0;}
    }else if(phase_==DialoguePhase::waiting){++idle_elapsed_;if(fast_forward)advance();}
}
void SceneDialogue::advance(){
    if(!assets_ready_)return;
    if(phase_==DialoguePhase::typing){budget_=duration();phase_=DialoguePhase::waiting;idle_elapsed_=0;return;}
    if(phase_==DialoguePhase::waiting){
        if(page_+1<body_.metrics().pages.size()){++page_;budget_=0;phase_=DialoguePhase::typing;}
        else phase_=DialoguePhase::complete;
    }
}
std::optional<CommandResult> SceneDialogue::dispatch(Command& command){
    const auto count=[&](std::size_t n){if(command.arguments.size()!=n)throw Error("invalid native dialogue parameters");};
    switch(command.index){
    case 142:{
        count(3);const auto speaker=command.arguments[0].integer();
        if(speaker!=-1)name(text::speaker_name(speaker),speaker);
        const auto frames=command.arguments[2].integer();if(frames>=0)settings_.open_ticks=std::min(frames,300);
        transparent_=command.arguments[1].integer()!=0;open();return CommandResult{true,true,Value(1)};
    }
    case 144:
        count(1);
        if(!command.continuation){
            const auto& arg=command.arguments[0];if(arg.type()==Type::text)name(arg.text(),-1);
            else{const auto id=arg.integer();if(id==-1)name({},-1);else name(text::speaker_name(id),id);}
            open();command.continuation=1;
        }
        return CommandResult{assets_ready_,!assets_ready_,Value(1)};
    case 130:case 131:case 133:
        count(3);
        if(!command.continuation){
            const auto replace=command.arguments[2].integer();if(replace!=0 && replace!=1)throw Unsupported("native message append mode "+std::to_string(replace));
            open();message(command.arguments[0].text(),command.arguments[1].integer(),replace!=0);command.continuation=1;
        }
        return CommandResult{command.index==133 || complete(),command.index!=133,Value(1)};
    case 134:count(0);return CommandResult{complete(),true,Value(1)};
    // Existing 0x454cb0 -> 0x40e7a0 conditionally resets the scene camera;
    // its generic argument is ignored. It does not replay/stop dialogue audio.
    case 132:count(1);if(end_message_)end_message_();return CommandResult{true,false,Value(1)};
    case 143:
        count(2);
        if(command.arguments[1].integer()!=0)throw Unsupported("native dialogue window close effect "+std::to_string(command.arguments[1].integer()));
        if(phase_!=DialoguePhase::closed){const auto ticks=command.arguments[0].integer();
            close_ticks_=std::uint32_t(ticks<0?settings_.open_ticks:std::min(ticks,100000));open_elapsed_=0;phase_=DialoguePhase::closing;
            if(!close_ticks_)update(true);}
        return CommandResult{true,false,Value(1)};
    default:return std::nullopt;
    }
}
std::span<const image::Sprite> SceneDialogue::sprites(){
    sprites_.clear();if(!visible_ || !assets_ready_ || phase_==DialoguePhase::closed)return sprites_;
    const auto add=[&](std::size_t image,float x,float y,float width,float height,float sx,float sy,float sw,float sh,std::uint8_t alpha=255){
        if(!alpha)return;const auto& source=images_[image];if(!source)throw Error("native dialogue image unavailable");
        sprites_.push_back({source,x,y,width,height,sx,sy,sw,sh,255,255,255,alpha});
    };
    const auto alpha=phase_==DialoguePhase::opening && settings_.open_ticks?std::uint8_t(std::min(255u,open_elapsed_*255/std::uint32_t(settings_.open_ticks))):
        phase_==DialoguePhase::closing && close_ticks_?std::uint8_t(255-std::min(255u,open_elapsed_*255/close_ticks_)):std::uint8_t(255);
    if(settings_.window){add(0,16,496,1248,208,0,0,1248,208,transparent_?0:std::uint8_t((picture_?settings_.picture_alpha:settings_.window_alpha)*alpha/255));
        add(1,16,496,1248,208,0,0,1248,208,alpha);}
    static constexpr std::array<int,9> button_x{808,848,928,968,1008,1048,1088,1128,1168};
    for(std::size_t i=0;i<button_x.size();++i)add(2,float(button_x[i]),680,40,40,float(i*40),0,40,40,std::uint8_t(settings_.button_alpha*alpha/255));
    add(3,0,672,1280,48,208,104,120,48,alpha);
    if(has_text_){
        for(std::size_t prior=0;prior<page_;++prior)body_.append(sprites_,std::uint32_t(prior),std::numeric_limits<std::uint32_t>::max()/32,274,536,alpha);
        body_.append(sprites_,std::uint32_t(page_),budget_,274,536,alpha);
    }
    const auto name_alpha=std::uint8_t(name_elapsed_*255/10);
    const auto offset=float(10-name_elapsed_)*float(10-name_elapsed_)*.32f;
    if(!name_.empty())name_text_.append(sprites_,0,100000,274-offset,508,std::uint8_t(name_alpha*alpha/255));
    if(name_elapsed_<10)old_name_text_.append(sprites_,0,100000,274+32-offset,508,std::uint8_t((255-name_alpha)*alpha/255));
    return sprites_;
}
}
