#include "confirmation_view.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace wa2::game {
ConfirmationView::ConfirmationView(Loader loader,Text text,Sound sound):loader_(std::move(loader)),text_(std::move(text)),sound_(std::move(sound)){
    if(!loader_ || !text_)throw script::Error("Missing native confirmation image or text loader");
    black_=std::make_shared<const image::Raster>(image::Raster{1,1,{0,0,0,255}});sprites_.reserve(80);
}
void ConfirmationView::prepare(std::string_view prompt,int skin,unsigned opening,unsigned closing){
    if(prompt.empty() || prompt.size()>4096 || skin<0 || skin>9 || opening>600 || closing>600)throw script::Error("Invalid native confirmation prompt, theme or timing");
    auto label=text_(prompt);std::array<char,32> filename{};std::snprintf(filename.data(),filename.size(),"sys_%01d0100.tga",skin);
    auto request=loader_(filename.data());if(!request)throw script::Error("Missing native confirmation image request");
    label_=std::move(label);request_=std::move(request);images_={};sprites_.clear();focus_.reset();pressed_.reset();answer_.reset();elapsed_=0;open_=opening;close_=closing;phase_=ConfirmationPhase::loading;
}
bool ConfirmationView::poll(){
    for(unsigned n=0;n<images_.size();++n)if(!images_[n]){
        if(!request_){request_=loader_("sys_00101.tga");if(!request_)throw script::Error("Missing native confirmation button request");}
        auto image=request_->get();if(!image)return false;
        if(image->width<(n?320u:544u) || image->height<(n?120u:216u))throw script::Unsupported("Native confirmation atlas dimensions are not supported");
        images_[n]=std::move(image);request_.reset();
    }
    return true;
}
void ConfirmationView::choose(bool yes){
    answer_=yes;pressed_=yes?0u:1u;elapsed_=0;phase_=ConfirmationPhase::closing;
    if(!yes && sound_)sound_(audio::SystemCue::cancel);
}
std::optional<bool> ConfirmationView::take_answer(){const auto result=answer_;answer_.reset();return result;}
void ConfirmationView::update(ConfirmationInput input){
    const auto& in=input.selection;
    if(!std::isfinite(in.x) || !std::isfinite(in.y) || std::abs(std::int64_t(in.navigation))>1)throw script::Error("Native confirmation input is out of range");
    if(phase_==ConfirmationPhase::idle)throw script::Error("Native confirmation is not prepared");
    if(phase_==ConfirmationPhase::closed)return;
    if(input.cancel && (phase_==ConfirmationPhase::loading || phase_==ConfirmationPhase::opening)){request_.reset();choose(false);}
    if(phase_==ConfirmationPhase::loading){if(!poll())return;phase_=ConfirmationPhase::opening;elapsed_=0;}
    switch(phase_){
    case ConfirmationPhase::opening:if(++elapsed_>=open_){elapsed_=0;phase_=ConfirmationPhase::active;}break;
    case ConfirmationPhase::active:{
        const auto old=focus_;pressed_.reset();
        if(in.pointer){focus_.reset();if(in.y>=376 && in.y<416){if(in.x>=472 && in.x<632)focus_=0;else if(in.x>=648 && in.x<808)focus_=1;}}
        if(in.navigation)focus_=focus_?1u-*focus_:in.navigation>0?0u:1u;
        if(focus_ && old!=focus_ && sound_)sound_(audio::SystemCue::focus);
        if(input.cancel)choose(false);else if(focus_ && (in.confirm || (in.pointer && in.activate)))choose(*focus_==0);
        break;
    }
    case ConfirmationPhase::closing:if(++elapsed_>=close_)phase_=ConfirmationPhase::closed;break;
    default:break;
    }
    draw();
}
void ConfirmationView::draw(){
    sprites_.clear();if(!images_[0] || !images_[1] || phase_==ConfirmationPhase::closed)return;
    const auto fraction=phase_==ConfirmationPhase::opening?(open_?std::min(1.f,float(elapsed_)/open_):1.f):phase_==ConfirmationPhase::closing?(close_?1-std::min(1.f,float(elapsed_)/close_):0.f):1.f;
    const auto alpha=std::uint8_t(std::clamp(int(256*fraction),0,255));
    sprites_.push_back({black_,0,0,1280,720,0,0,1,1,255,255,255,std::uint8_t(76*fraction)});sprites_.back().layer=0x1066;sprites_.back().order=0x537;
    // Cached 41e3f1..41e42a uses 656x264. The external 544x216 atlas is
    // bounded by scene_quad at its full edges, rather than cut to 128 rows.
    sprites_.push_back({images_[0],368,256,656,264,0,0,656,264,255,255,255,alpha});sprites_.back().layer=0x1066;sprites_.back().order=0x533;
    for(unsigned n=0;n<2;++n){const auto row=pressed_==n?2u:focus_==n?1u:0u;sprites_.push_back({images_[1],float(n?648:472),376,160,40,float(n*160),float(row*40),160,40,255,255,255,alpha});sprites_.back().layer=0x1067;sprites_.back().order=0x534+int(n);}
    const auto first=sprites_.size();label_.append(sprites_,0,std::numeric_limits<std::uint32_t>::max(),516,304,alpha);
    for(auto n=first;n<sprites_.size();++n){sprites_[n].layer=0x1067;sprites_[n].order=0x530;}
    std::stable_sort(sprites_.begin(),sprites_.end(),[](const auto& a,const auto& b){return a.layer==b.layer?a.order<b.order:a.layer<b.layer;});
}
std::string_view options_reset_prompt()noexcept{
    // Original font indices, cached 0x4a2f34. No platform text conversion.
    return "<F20\x81\x40\x8f\x70\x90\xfc\x98\x48\x95\xfa\x99\x94\x8c\x60\x9b\x96\x97\xf3\x98\x48\x95\xfa\x81\x40\n\x81\x40\x81\x40\x81\x40\x81\x40\x94\xbb\x8d\x73\x8b\x4c\x81\x48\x81\x40\x81\x40>";
}
}
