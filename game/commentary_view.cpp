#include "commentary_view.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace wa2::game {
namespace {
constexpr std::array<unsigned,5> speakers{4,2,1,3,5};
constexpr std::array<std::string_view,6> names{"sys_07000.tga","sys_09000.tga","sys_07100.tga","sys_07020.tga","sys_07021.tga","sys_09100.tga"};
constexpr unsigned widths[]{1280,1280,1280,160,288,56},heights[]{40,56,720,120,40,72};
}
CommentaryView::CommentaryView(Loader loader,Sound sound):loader_(std::move(loader)),sound_(std::move(sound)){if(!loader_)throw script::Error("Missing native commentary image loader");sprites_.reserve(12);}
void CommentaryView::clear(){images_={};request_.reset();page_.reset();focus_.reset();command_.reset();sprites_.clear();phase_=CommentaryPhase::idle;next_=speaker_=0;}
void CommentaryView::prepare(){auto request=loader_(names[0]);if(!request)throw script::Error("Missing native commentary image request");clear();request_=std::move(request);phase_=CommentaryPhase::loading;}
bool CommentaryView::images(){
    try{for(unsigned issued=0;next_<images_.size();){if(!request_){if(issued==2)return false;request_=loader_(names[next_]);++issued;if(!request_)throw script::Error("Missing native commentary image request");}
            auto image=request_->get();if(!image)return false;if(image->width<widths[next_] || image->height<heights[next_])throw script::Unsupported("Native commentary atlas dimensions are not supported: "+std::string(names[next_]));images_[next_++]=std::move(image);request_.reset();}
    }catch(...){request_.reset();phase_=CommentaryPhase::idle;throw;}return true;
}
void CommentaryView::failed(){request_.reset();page_.reset();focus_.reset();command_.reset();phase_=next_==images_.size()?CommentaryPhase::menu:CommentaryPhase::idle;draw();}
void CommentaryView::activate(unsigned id){
    if(id==50){command_=CommentaryCommand{CommentaryCommand::Kind::close};request_.reset();page_.reset();phase_=CommentaryPhase::closed;sprites_.clear();if(sound_)sound_(audio::SystemCue::cancel);return;}
    if(id==33){command_=CommentaryCommand{CommentaryCommand::Kind::stop};request_.reset();page_.reset();phase_=CommentaryPhase::menu;focus_.reset();if(sound_)sound_(audio::SystemCue::cancel);draw();return;}
    if(id>=speakers.size() || phase_!=CommentaryPhase::menu)throw script::Error("Unknown native commentary control");
    const auto speaker=speakers[id];std::array<char,32> name{};std::snprintf(name.data(),name.size(),"sys_072%02u.tga",speaker-1);auto request=loader_(name.data());if(!request)throw script::Error("Missing native commentary page request");request_=std::move(request);speaker_=speaker;phase_=CommentaryPhase::text_loading;if(sound_)sound_(audio::SystemCue::accept);
}
std::optional<CommentaryCommand> CommentaryView::take_command(){auto command=command_;command_.reset();return command;}
void CommentaryView::update(CommentaryInput input,bool playing){
    const auto& selected=input.selection;if(!std::isfinite(selected.x) || !std::isfinite(selected.y) || std::abs(std::int64_t(selected.navigation))>1)throw script::Error("Native commentary input is out of range");
    if(phase_==CommentaryPhase::idle)throw script::Error("Native commentary is not prepared");if(phase_==CommentaryPhase::closed || command_)return;
    if(input.cancel){activate(phase_==CommentaryPhase::listening || phase_==CommentaryPhase::text_loading?33:50);return;}
    if(phase_==CommentaryPhase::loading){if(!images())return;phase_=CommentaryPhase::menu;draw();return;}
    if(phase_==CommentaryPhase::text_loading){try{auto image=request_->get();if(!image)return;if(image->width<1280 || image->height<720)throw script::Unsupported("Native commentary page must cover 1280x720");page_=std::move(image);request_.reset();phase_=CommentaryPhase::listening;focus_.reset();command_=CommentaryCommand{CommentaryCommand::Kind::play,speaker_};draw();}catch(...){failed();throw;}return;}
    if(phase_==CommentaryPhase::listening && !playing){failed();command_=CommentaryCommand{CommentaryCommand::Kind::stop};return;}
    const auto previous=focus_;if(selected.pointer){focus_.reset();if(phase_==CommentaryPhase::menu && selected.x>=0 && selected.x<1280 && selected.y>=29 && selected.y<675)focus_=unsigned(selected.x)/256;
        if(phase_==CommentaryPhase::listening && selected.x>=1056 && selected.x<1216 && selected.y>=624 && selected.y<664)focus_=33;
        if(selected.x>=1164 && selected.x<1220 && selected.y>=688 && selected.y<712)focus_=50;}
    if(selected.navigation){std::vector<unsigned> controls;if(phase_==CommentaryPhase::menu)controls={0,1,2,3,4,50};else controls={33,50};const auto at=std::find(controls.begin(),controls.end(),focus_.value_or(999));const int size=int(controls.size());const auto index=at==controls.end()?(selected.navigation>0?0:size-1):(int(at-controls.begin())+selected.navigation+size)%size;focus_=controls[unsigned(index)];}
    if(focus_ && focus_!=previous && sound_)sound_(audio::SystemCue::focus);if(focus_ && (selected.confirm || (selected.pointer && selected.activate)))activate(*focus_);draw();
}
void CommentaryView::draw(){
    sprites_.clear();if(next_<images_.size() || phase_==CommentaryPhase::closed)return;int order{};
    const auto add=[&](std::shared_ptr<const image::Raster> image,float x,float y,float w,float h,float sx,float sy,float sw,float sh,unsigned shade=255){sprites_.push_back({std::move(image),x,y,w,h,sx,sy,sw,sh,std::uint8_t(shade),std::uint8_t(shade),std::uint8_t(shade)});sprites_.back().layer=0x1060;sprites_.back().order=order++;};
    if(page_){add(page_,0,0,1280,720,0,0,1280,720);add(images_[3],1056,624,160,40,0,focus_==33?40:0,160,40);}
    else{add(images_[2],0,0,1280,720,0,0,1280,720,127);if(focus_ && *focus_<5)add(images_[2],float(*focus_*256),29,256,646,float(*focus_*256),29,256,646);add(images_[4],496,608,288,40,0,0,288,40);}
    add(images_[0],0,0,1280,40,0,0,1280,40);add(images_[1],0,664,1280,56,0,0,1280,56);add(images_[5],1164,688,56,24,0,focus_==50?24:0,56,24);
}
}
