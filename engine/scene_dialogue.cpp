#include "scene_dialogue.hpp"
#include "checkpoint_codec.hpp"
#include "resource_catalog.hpp"
#include <algorithm>
#include <cstdio>
#include <limits>

namespace wa2::script {
SceneDialogue::SceneDialogue(text::FontBook& fonts,Loader loader,std::function<void(std::int32_t)> seen,std::function<void()> end_message)
    :fonts_(fonts),loader_(std::move(loader)),seen_(std::move(seen)),end_message_(std::move(end_message)){
    if(!loader_)throw Error("missing native dialogue image loader");sprites_.reserve(512);
    black_=std::make_shared<image::Raster>(image::Raster{1,1,{0,0,0,255}});
}
void SceneDialogue::settings(DialogueSettings value){
    if(value.skin<0 || value.skin>9 || value.window_alpha<0 || value.window_alpha>255 || value.picture_alpha<0 || value.picture_alpha>255 ||
       value.button_alpha<0 || value.button_alpha>255 || value.text_step<0 || value.text_step>1000 || value.open_ticks<0 || value.open_ticks>300 ||
       value.novel_alpha<0 || value.novel_alpha>255)
        throw Error("invalid native dialogue settings");
    if(assets_started_ && value.skin!=settings_.skin){requests_={};images_={};assets_started_=assets_ready_=false;}
    settings_=value;
    novel_.alpha(value.novel_alpha);
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
void SceneDialogue::prepare_restored_assets(resource::Catalog& resources,const image::Resolve& resolve){
    if(phase_==DialoguePhase::closed)return;load_assets();
    for(unsigned n=0;n<requests_.size();++n)if(requests_[n]){images_[n]=image::resolve_reference(resources.reference(requests_[n]),resources,resolve);requests_[n].reset();}assets_ready_=true;
}
void SceneDialogue::open(){
    load_assets();visible_=true;
    if(phase_==DialoguePhase::closed || phase_==DialoguePhase::closing){phase_=DialoguePhase::opening;open_elapsed_=0;}
}
void SceneDialogue::name(std::string_view value,std::int32_t speaker){
    speaker_=speaker;
    if(value==name_)return;
    if(!value.empty() && !name_.empty())name_slot_^=1;
    old_name_text_=std::move(name_text_);name_=value;name_elapsed_=0;
    if(name_.empty()){name_text_={};return;}
    text::TextStyle style;style.columns=36;style.rows=1;style.spacing=2;style.line_gap=0;style.colour=text::speaker_colour(text::speaker_style(speaker_,name_));
    name_text_=fonts_.defer(text::layout(name_,style));
}
void SceneDialogue::message(std::string_view value,std::int32_t id,bool replace){
    const bool append=!replace && !raw_.empty();const auto previous_pages=body_.metrics().pages.size();
    if(append)raw_+="\\k";else raw_.clear();
    raw_+=value;text::TextStyle style;style.colour=text::speaker_colour(text::speaker_style(speaker_,name_));
    if(novel_.enabled()){style.columns=40;style.rows=15;}
    body_=fonts_.defer(text::layout(raw_,style));
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
    novel_.update([&](auto ticks){return fast_forward?0:ticks==-1?15:ticks==-2?30:ticks;});
    if(!assets_ready_ && (assets_started_ || phase_!=DialoguePhase::closed))load_assets();
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
    if(auto result=novel_.dispatch(command))return result;
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
    // Existing 0x454cb0 -> 0x40e7a0 stops a live nonzero-duration shake.
    // Its argument is ignored; continuous shakes remain active.
    case 132:count(1);if(!end_message_)throw Unsupported("Missing native EndMessage shake binding");end_message_();return CommandResult{true,false,Value(1)};
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
std::vector<std::uint8_t> SceneDialogue::checkpoint()const{
    checkpoint::Writer out;out.u32(2);
    for(auto v:{settings_.skin,settings_.window_alpha,settings_.picture_alpha,settings_.button_alpha,settings_.text_step,settings_.open_ticks,settings_.novel_alpha})out.i32(v);
    out.boolean(settings_.window);out.blob(novel_.checkpoint());
    out.blob(text::checkpoint_layout(body_.metrics()));out.blob(text::checkpoint_layout(name_text_.metrics()));out.blob(text::checkpoint_layout(old_name_text_.metrics()));
    out.string(raw_);out.string(name_);out.u32(std::uint32_t(phase_));out.u32(budget_);out.u32(open_elapsed_);out.u32(name_elapsed_);out.u32(idle_elapsed_);out.u32(close_ticks_);out.u32(std::uint32_t(page_));
    out.i32(speaker_);out.i32(message_);for(auto value:{visible_,picture_,has_text_,fast_,transparent_})out.boolean(value);out.u32(name_slot_);
    return out.seal("LSDI");
}
void SceneDialogue::restore(std::span<const std::uint8_t> bytes){
    auto in=checkpoint::Reader::open("LSDI",bytes);SceneDialogue next(fonts_,loader_,seen_,end_message_);DialogueSettings settings;
    if(in.u32()!=2)throw Error("Unsupported saved dialogue presentation version");
    settings.skin=in.i32();settings.window_alpha=in.i32();settings.picture_alpha=in.i32();settings.button_alpha=in.i32();settings.text_step=in.i32();settings.open_ticks=in.i32();settings.novel_alpha=in.i32();settings.window=in.boolean();next.settings(settings);
    next.novel_.restore(in.blob(1024));auto body=text::restore_layout(in.blob(2u*1024*1024));auto name=text::restore_layout(in.blob(2u*1024*1024));auto old_name=text::restore_layout(in.blob(2u*1024*1024));
    next.raw_=in.string(4095);next.name_=in.string(4095);const auto phase=in.u32();
    next.budget_=in.u32();next.open_elapsed_=in.u32();next.name_elapsed_=in.u32();next.idle_elapsed_=in.u32();next.close_ticks_=in.u32();next.page_=in.u32();
    next.speaker_=in.i32();next.message_=in.i32();next.visible_=in.boolean();next.picture_=in.boolean();next.has_text_=in.boolean();next.fast_=in.boolean();next.transparent_=in.boolean();next.name_slot_=in.u32();in.finish();
    if(phase>std::uint32_t(DialoguePhase::closing) || next.open_elapsed_>100001 || next.name_elapsed_>10 || next.name_slot_>1 || next.close_ticks_>100000 || next.page_>=4096 || next.speaker_<-1 || next.speaker_>100 || next.message_<-1 ||
       next.raw_.find('\0')!=std::string::npos || next.name_.find('\0')!=std::string::npos ||
       (next.has_text_ && (next.page_>=body.pages.size() || std::uint64_t(next.budget_)>std::uint64_t(body.pages[next.page_].duration)+160)) ||
       (!next.has_text_ && (phase==std::uint32_t(DialoguePhase::typing) || phase==std::uint32_t(DialoguePhase::waiting))))
        throw Error("Invalid saved dialogue state");
    next.phase_=DialoguePhase(phase);
    // Rebuild only transient glyph textures. Neither dialogue history nor
    // script callbacks are replayed; original resources remain external.
    next.body_=fonts_.defer(std::move(body));next.name_text_=fonts_.defer(std::move(name));next.old_name_text_=fonts_.defer(std::move(old_name));
    settings_=next.settings_;novel_=std::move(next.novel_);body_=std::move(next.body_);name_text_=std::move(next.name_text_);old_name_text_=std::move(next.old_name_text_);
    raw_=std::move(next.raw_);name_=std::move(next.name_);phase_=next.phase_;budget_=next.budget_;open_elapsed_=next.open_elapsed_;name_elapsed_=next.name_elapsed_;name_slot_=next.name_slot_;idle_elapsed_=next.idle_elapsed_;close_ticks_=next.close_ticks_;page_=next.page_;
    speaker_=next.speaker_;message_=next.message_;visible_=next.visible_;picture_=next.picture_;has_text_=next.has_text_;fast_=next.fast_;transparent_=next.transparent_;
    requests_={};images_={};sprites_.clear();assets_started_=assets_ready_=false;
}
std::span<const image::Sprite> SceneDialogue::sprites(){
    sprites_.clear();if(!visible_)return sprites_;
    if(novel_.enabled() && novel_.opacity()){sprites_.push_back({black_,0,0,1280,720,0,0,1,1,255,255,255,novel_.opacity()});sprites_.back().layer=0x104c;sprites_.back().order=0x41d;}
    if(!assets_ready_ || phase_==DialoguePhase::closed)return sprites_;
    // Cached 0x402cd0, 0x4033b0, 0x40e564 and 0x402beb establish the
    // source layers/slots. The bottom strip (0x418), like novel backing,
    // is not among the message-shake items updated by 0x403f30.
    const auto add=[&](std::size_t image,float x,float y,float width,float height,float sx,float sy,float sw,float sh,std::uint8_t alpha=255,int order=-1){
        if(!alpha)return;const auto& source=images_[image];if(!source)throw Error("native dialogue image unavailable");
        sprites_.push_back({source,x,y,width,height,sx,sy,sw,sh,255,255,255,alpha});
        constexpr int layers[]{0x104d,0x104e,0x104f,0x104e},orders[]{0x40d,0x40e,0x40f,0x418};
        auto& item=sprites_.back();item.layer=layers[image];item.order=order<0?orders[image]:order;item.space=image==3?image::SpriteSpace::screen:image::SpriteSpace::dialogue;
    };
    const auto tag=[&](std::size_t first,int layer,int order){for(;first<sprites_.size();++first){auto& item=sprites_[first];item.layer=layer;item.order=order;item.space=image::SpriteSpace::dialogue;}};
    const auto alpha=phase_==DialoguePhase::opening && settings_.open_ticks?std::uint8_t(std::min(255u,open_elapsed_*255/std::uint32_t(settings_.open_ticks))):
        phase_==DialoguePhase::closing && close_ticks_?std::uint8_t(255-std::min(255u,open_elapsed_*255/close_ticks_)):std::uint8_t(255);
    if(settings_.window && !novel_.enabled()){add(0,16,496,1248,208,0,0,1248,208,transparent_?0:std::uint8_t((picture_?settings_.picture_alpha:settings_.window_alpha)*alpha/255));
        add(1,16,496,1248,208,0,0,1248,208,alpha);}
    static constexpr std::array<int,9> button_x{808,848,928,968,1008,1048,1088,1128,1168};
    for(std::size_t i=0;i<button_x.size();++i)add(2,float(button_x[i]),680,40,40,float(i*40),0,40,40,std::uint8_t(settings_.button_alpha*alpha/255),0x40f+int(i));
    add(3,0,672,1280,48,208,104,120,48,alpha);
    if(has_text_ && (!novel_.enabled() || novel_.text_visible())){
        const auto first=sprites_.size();
        if(page_ || budget_)fonts_.materialize(body_);
        const float x=novel_.enabled()?75.f:274.f,y=novel_.enabled()?26.f:536.f;
        for(std::size_t prior=0;prior<page_;++prior)body_.append(sprites_,std::uint32_t(prior),std::numeric_limits<std::uint32_t>::max()/32,x,y,alpha);
        body_.append(sprites_,std::uint32_t(page_),budget_,x,y,alpha);
        tag(first,0x1057,0x443);
    }
    const auto name_alpha=std::uint8_t(name_elapsed_*255/10);
    const auto offset=float(10-name_elapsed_)*float(10-name_elapsed_)*.32f;
    if(!novel_.enabled()){
        if(!name_.empty() && name_alpha){const auto first=sprites_.size();fonts_.materialize(name_text_);name_text_.append(sprites_,0,100000,274-offset,508,std::uint8_t(name_alpha*alpha/255));tag(first,0x105b,0x444+int(name_slot_));}
        if(name_elapsed_<10){const auto first=sprites_.size();fonts_.materialize(old_name_text_);old_name_text_.append(sprites_,0,100000,274+32-offset,508,std::uint8_t((255-name_alpha)*alpha/255));tag(first,0x105b,0x444+int(name_slot_^1));}
    }
    return sprites_;
}
}
