#include "quick_view.hpp"
#include "save_view.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace wa2::game {
namespace {
text::TextStyle result_style(){text::TextStyle style;style.size=28;style.columns=20;style.rows=1;style.line_gap=0;return style;}
text::TextStyle confirmation_style(){auto style=result_style();style.columns=15;style.rows=2;return style;}
}
QuickView::QuickView(Loader loader,Text text,Sound sound):loader_(std::move(loader)),text_(std::move(text)),sound_(std::move(sound)){
    if(!loader_ || !text_)throw script::Error("Missing native quick-operation services");
    black_=std::make_shared<const image::Raster>(image::Raster{1,1,{0,0,0,255}});sprites_.reserve(96);
}
void QuickView::clear(){context_={};request_.reset();result_image_.reset();confirmation_.reset();label_={};sprites_.clear();command_.reset();phase_=QuickPhase::idle;elapsed_=0;committed_=false;}
void QuickView::prepare(QuickContext context){
    context.timing.validate();if((context.kind!=QuickKind::save && context.kind!=QuickKind::load) || context.skin<0 || context.skin>9 || !context.result_ticks || context.result_ticks>600)throw script::Error("Invalid native quick-operation context");
    auto label=text_(context.kind==QuickKind::save?save_result_prompt():quick_load_result_prompt(),result_style());
    auto request=loader_("sys_"+std::to_string(context.skin)+"0110.tga");if(!request)throw script::Error("Missing native quick-operation image request");
    clear();context_=context;label_=std::move(label);request_=std::move(request);phase_=QuickPhase::loading;
}
void QuickView::closed(){request_.reset();confirmation_.reset();sprites_.clear();phase_=QuickPhase::closed;command_=QuickCommand::close;}
void QuickView::result(){confirmation_.reset();phase_=QuickPhase::result;elapsed_=0;}
void QuickView::succeeded(){
    if(committed_ || !((context_.kind==QuickKind::save && phase_==QuickPhase::loading) || (context_.kind==QuickKind::load && phase_==QuickPhase::awaiting)))throw script::Error("Native quick operation has no pending commit");
    committed_=true;if(phase_==QuickPhase::awaiting){if(confirmation_)phase_=QuickPhase::confirming;else result();}draw();
}
std::optional<QuickCommand> QuickView::take_command(){const auto next=command_;command_.reset();return next;}
void QuickView::update(ConfirmationInput input){
    const auto& in=input.selection;if(!std::isfinite(in.x) || !std::isfinite(in.y) || std::abs(std::int64_t(in.navigation))>1)throw script::Error("Native quick-operation input is out of range");
    if(phase_==QuickPhase::idle)throw script::Error("Native quick operation is not prepared");if(phase_==QuickPhase::closed || command_)return;
    if(phase_==QuickPhase::loading){
        if(input.cancel && context_.kind==QuickKind::load){if(sound_)sound_(audio::SystemCue::cancel);closed();return;}
        auto image=request_->get();if(!image)return;if(image->width<480 || image->height<112)throw script::Unsupported("Native quick-result atlas dimensions are not supported");
        if(context_.kind==QuickKind::save && !committed_)throw script::Error("Native quick-save result requires a committed save");
        result_image_=std::move(image);request_.reset();
        if(context_.kind==QuickKind::save)result();
        else if(context_.confirmation){auto confirmation=std::make_unique<ConfirmationView>(loader_,[this](auto raw){return text_(raw,confirmation_style());},sound_);confirmation->prepare(quick_load_prompt(),context_.skin,context_.timing.open,context_.timing.close);confirmation_=std::move(confirmation);phase_=QuickPhase::confirming;draw();return;}
        else{phase_=QuickPhase::awaiting;command_=QuickCommand::load;return;}
    }
    if(phase_==QuickPhase::confirming){
        confirmation_->update(committed_?ConfirmationInput{}:input);
        if(const auto answer=confirmation_->take_answer();answer && *answer){phase_=QuickPhase::awaiting;command_=QuickCommand::load;}
        else if(confirmation_->phase()==ConfirmationPhase::closed){if(committed_)result();else{closed();return;}}
    }else if(phase_==QuickPhase::result){
        if(++elapsed_>=std::max(context_.result_ticks,context_.timing.page)){phase_=QuickPhase::closing;elapsed_=0;}
    }else if(phase_==QuickPhase::closing){if(++elapsed_>=context_.timing.close){closed();return;}}
    draw();
}
void QuickView::draw(){
    sprites_.clear();if(confirmation_){const auto items=confirmation_->sprites();sprites_.insert(sprites_.end(),items.begin(),items.end());return;}
    if(phase_!=QuickPhase::result || !result_image_)return;
    const auto fraction=context_.timing.page?std::min(1.f,float(elapsed_)/context_.timing.page):1.f;
    const auto alpha=std::uint8_t(std::min(255,int(256*fraction)));
    sprites_.push_back({black_,0,0,1280,720,0,0,1,1,255,255,255,76});sprites_.back().layer=0x1066;sprites_.back().order=0x4f1;
    sprites_.push_back({result_image_,400,304,480,112,0,0,480,112,255,255,255,alpha});sprites_.back().layer=0x1067;sprites_.back().order=0x4f4;
    const auto first=sprites_.size();label_.append(sprites_,0,std::numeric_limits<std::uint32_t>::max(),450,345,alpha);for(auto n=first;n<sprites_.size();++n){sprites_[n].layer=0x1068;sprites_[n].order=0x4f5;}
}
std::string_view quick_load_prompt()noexcept{return "<F20\x81\x40\x81\x40\x81\x40\x8e\xa6\x99\xb6\x98\x6d\x8a\xe0\x8d\x5c\x91\x94\x81\x42\x81\x40\x81\x40\n\x81\x40\x81\x40\x81\x40\x81\x40\x94\xbb\x8d\x73\x8b\x4c\x81\x48\x81\x40\x81\x40>";}
std::string_view quick_load_result_prompt()noexcept{return "<F20\x81\x40\x81\x40\x81\x40\x8d\x5c\x91\x94\x98\x6d\x8a\xe0\x8f\x62\x8a\x85\x81\x40\x81\x40\x81\x40\x81\x40>";}
}
