#include "scene_background.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace wa2::script {
SceneBackground::SceneBackground(Loader loader,std::function<void(bool)> changing,Loader masks,Loader events)
    :loader_(std::move(loader)),mask_loader_(std::move(masks)),event_loader_(std::move(events)),changing_(std::move(changing)){
    if(!loader_)throw Error("missing native background loader");
}
void SceneBackground::begin(Command& command,std::int32_t time_mode){
    if(command.arguments.size()!=9)throw Error("invalid native background parameters");
    const auto integer=[&](std::size_t n){return command.arguments[n].integer();};
    const auto effect=integer(0),number=integer(1),variant=integer(2);
    const auto width=command.arguments[7].real(),height=command.arguments[8].real();
    const bool masked=effect>=128 && (effect&~0x19ff)==0;
    if(effect!=-1 && effect!=0 && effect!=1 && !masked)throw Unsupported("unimplemented native background effect "+std::to_string(effect));
    const bool event=command.index==148 || command.index==149;
    if(number<0 || number>(event?99999:9999) || variant<0 || variant>9 || time_mode<0 || time_mode>9)
        throw Unsupported("unimplemented native background image selector");
    if((!masked && integer(4)!=0) || (masked && (integer(4)<0 || integer(4)>3)) ||
       integer(5)==10000 || integer(6)==10000 || !std::isfinite(width) || !std::isfinite(height) ||
       width<=0 || height<=0 || width>=10000 || height>=10000)
        throw Unsupported("unimplemented native background transform");
    std::array<char,32> name{};
    // 0x455050/0x4551b0 choose *10 for this game's GameTitle. The *100
    // compatibility branch in the shared engine belongs to a different title.
    if(event)std::snprintf(name.data(),name.size(),"%c%06d.tga",command.index==148?'v':'h',int(number*10+variant));
    else std::snprintf(name.data(),name.size(),"B%04d%d%d.tga",int(number),int(variant),int(time_mode));
    if(event && !event_loader_)throw Unsupported("missing native event-image loader");
    auto request=(event?event_loader_:loader_)(name.data());if(!request)throw Error("native background loader returned no image request");
    std::shared_ptr<image::ImageRequest> mask_request;
    if(masked && integer(3)>0){
        if(!mask_loader_)throw Unsupported("missing native background mask loader");
        std::snprintf(name.data(),name.size(),"f0%03d.bmp",int(effect&0x7f));
        mask_request=mask_loader_(name.data());if(!mask_request)throw Error("native background mask loader returned no request");
    }
    if(changing_)changing_(command.index==147);
    picture_=event;
    request_=std::move(request);current_^=1;layers_[current_].reset();
    mask_request_=std::move(mask_request);mask_.reset();mask_mode_=integer(4);
    effect_=effect==-1?1:effect;elapsed_=0;duration_=effect==-1?1:std::max(1,integer(3));
    x_=integer(5);y_=integer(6);width_=width;height_=height;loading_=true;active_=true;
}
std::optional<CommandResult> SceneBackground::dispatch(Command& command,std::int32_t time_mode){
    if(command.index<146 || command.index>149)return std::nullopt;
    if(!command.continuation){begin(command,time_mode);command.continuation=1;}
    return CommandResult{!busy(),true,Value{}};
}
void SceneBackground::update(const std::function<std::int32_t(std::int32_t)>& adjust){
    const auto duration=adjust?adjust(duration_):duration_;
    // A skip can shorten an in-flight transition. Drop a queued mask request
    // before waiting for it; the new background image itself is still needed.
    if(duration<=1){mask_request_.reset();mask_.reset();}
    if(loading_){
        auto raster=request_->get();if(!raster)return;
        if(mask_request_){auto mask=mask_request_->get();if(!mask)return;
            mask_=BackgroundFrame::Mask{std::move(mask),-1.f,1.f,(effect_&0x800)!=0,(effect_&0x1000)!=0,(effect_&0x100)!=0};
            mask_request_.reset();}
        const auto width=float(raster->width),height=float(raster->height);
        // The original scales about the 1280x720 screen centre, with image
        // placement initially centred and the script offset applied to it.
        const float sx=width_/1280.f,sy=height_/720.f;
        layers_[current_]=BackgroundLayer{std::move(raster),640+((1280-width)/2+x_-640)*sx,
            360+((720-height)/2+y_-360)*sy,width*sx,height*sy,0};
        request_.reset();loading_=false;
    }
    if(!active_)return;
    if(++elapsed_>=duration){layers_[current_]->opacity=255;layers_[current_^1].reset();mask_.reset();active_=false;return;}
    if(mask_){
        // Cached 0040b890.c and the original pf00..pf03.psh: integer alpha
        // first, then /256 and the shader's signed bias/output multiplier.
        const auto alpha=std::int64_t(elapsed_)*255/duration;
        constexpr float ramp[]{2.f,1.5f,1.25f,1.125f},gain[]{1.f,2.f,4.f,32.f};
        mask_->bias=float(alpha)*(1.f/256.f)*ramp[mask_mode_]-1.f;mask_->gain=gain[mask_mode_];
        layers_[current_]->opacity=255;return;
    }
    // Effect 0 requests a dissolve on the ordinary 2-D rendering path, as does
    // effect 1. Mask effects use the separate original shader parameters above.
    layers_[current_]->opacity=std::uint8_t(std::clamp(std::int64_t(elapsed_)*255/duration,std::int64_t(0),std::int64_t(255)));
}
BackgroundFrame SceneBackground::frame()const{
    return {layers_[current_^1],layers_[current_],loading_?std::nullopt:mask_};
}
}
