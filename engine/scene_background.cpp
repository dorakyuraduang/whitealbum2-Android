#include "scene_background.hpp"
#include "resource_catalog.hpp"
#include "checkpoint_codec.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>

namespace wa2::script {
SceneBackground::SceneBackground(Loader loader,std::function<void(bool)> changing,Loader masks,Loader events)
    :loader_(std::move(loader)),mask_loader_(std::move(masks)),event_loader_(std::move(events)),changing_(std::move(changing)){
    if(!loader_)throw Error("missing native background loader");
}
void SceneBackground::begin(Command& command,std::int32_t time_mode){
    const bool extended=command.index>=225;
    if(command.arguments.size()!=(extended?11u:9u))throw Error("invalid native background parameters");
    const auto integer=[&](std::size_t n){return command.arguments[n].integer();};
    const auto effect=integer(0),number=integer(1),variant=integer(2);
    const auto width=command.arguments[7].real(),height=command.arguments[8].real();
    const auto angle=extended?command.arguments[9].real():0.f;
    if(extended)command.arguments[10].integer();
    const bool reuse=number==-2 && variant==0;
    const bool masked=effect>=128 && (effect&~0x19ff)==0;
    const bool scaled=effect>=11 && effect<=14,sliding=effect>=16 && effect<=19;
    if(effect!=-1 && effect!=0 && effect!=1 && !scaled && !sliding && !masked)throw Unsupported("unimplemented native background effect "+std::to_string(effect));
    const bool event=command.index==148 || command.index==149 || command.index==227 || command.index==228;
    if((number<0 && !reuse) || number>(event?99999:9999) || variant<0 || variant>9 || time_mode<0 || time_mode>9)
        throw Unsupported("unimplemented native background image selector");
    // The mode selects a masked-transition shader. Plain dissolves retain but
    // never consume it; B2 scripts also pass mode 2 with the ordinary effect.
    if((masked && (integer(4)<0 || integer(4)>3)) ||
       !std::isfinite(width) || !std::isfinite(height) || width<=0 || height<=0 || width>10000 || height>10000 ||
       !std::isfinite(angle) || std::abs(angle)>1.e9f)
        throw Unsupported("unimplemented native background transform");
    std::array<char,32> name{};
    // 0x455050/0x4551b0 choose *10 for this game's GameTitle. The *100
    // compatibility branch in the shared engine belongs to a different title.
    if(event)std::snprintf(name.data(),name.size(),"%c%06d.tga",command.index==148 || command.index==227?'v':'h',int(number*10+variant));
    else std::snprintf(name.data(),name.size(),"B%04d%d%d.tga",int(number),int(variant),int(time_mode));
    if(!reuse && event && !event_loader_)throw Unsupported("missing native event-image loader");
    std::shared_ptr<image::ImageRequest> request;std::shared_ptr<const image::Raster> ready;
    if(reuse){if(!layers_[current_] || !layers_[current_]->image)throw Error("Cannot reuse a native background before its image is ready");ready=layers_[current_]->image;}
    else{request=(event?event_loader_:loader_)(name.data());if(!request)throw Error("native background loader returned no image request");}
    std::shared_ptr<image::ImageRequest> mask_request;
    if(masked && integer(3)>0){
        if(!mask_loader_)throw Unsupported("missing native background mask loader");
        std::snprintf(name.data(),name.size(),"f0%03d.bmp",int(effect&0x7f));
        mask_request=mask_loader_(name.data());if(!mask_request)throw Error("native background mask loader returned no request");
    }
    if(changing_)changing_(command.index==147 || command.index==226);
    picture_=event;
    request_=std::move(request);ready_image_=std::move(ready);current_^=1;layers_[current_].reset();
    mask_request_=std::move(mask_request);mask_.reset();mask_mode_=integer(4);
    effect_=effect==-1?1:effect;elapsed_=0;duration_=effect==-1?1:std::max(1,integer(3));
    x_=integer(5);y_=integer(6);width_=width;height_=height;angle_=angle;extended_=extended;loading_=true;active_=true;
}
std::optional<CommandResult> SceneBackground::dispatch(Command& command,std::int32_t time_mode){
    if(command.index>=210 && command.index<=214){if(loading_)return CommandResult{false,true,{}};return camera_.dispatch(command,unsigned(current_));}
    if((command.index<146 || command.index>149) && (command.index<225 || command.index>228))return std::nullopt;
    if(!command.continuation){begin(command,time_mode);command.continuation=1;}
    if(command.index>=225 && command.arguments[10].integer()==0)return CommandResult{!loading_,loading_,Value{}};
    return CommandResult{!busy(),true,Value{}};
}
void SceneBackground::update(const std::function<std::int32_t(std::int32_t)>& adjust){
    camera_.update(adjust);
    const auto duration=adjust?adjust(duration_):duration_;
    effective_duration_=duration;
    // A skip can shorten an in-flight transition. Drop a queued mask request
    // before waiting for it; the new background image itself is still needed.
    if(duration<=1){mask_request_.reset();mask_.reset();}
    if(loading_){
        auto raster=ready_image_?ready_image_:request_->get();if(!raster)return;
        if(mask_request_){auto mask=mask_request_->get();if(!mask)return;
            mask_=BackgroundFrame::Mask{std::move(mask),-1.f,1.f,(effect_&0x800)!=0,(effect_&0x1000)!=0,(effect_&0x100)!=0};
            mask_request_.reset();}
        const auto width=float(raster->width),height=float(raster->height);
        // Camera pan is the source rectangle, not a destination translation.
        // Ordinary BGs centre this source offset; event images use it directly.
        const auto previous=camera_.pose(unsigned(current_^1));
        CameraPose pose;pose.x=x_==10000?previous.x:float((picture_?0:(1280-std::int64_t(raster->width))/2)+x_);
        pose.y=y_==10000?previous.y:float((picture_?0:(720-std::int64_t(raster->height))/2)+y_);
        pose.scale_x=width_==10000?previous.scale_x:extended_?width_:width_/1280.f;
        pose.scale_y=height_==10000?previous.scale_y:extended_?height_:height_/720.f;
        pose.angle=angle_==10000?previous.angle:angle_;
        camera_.replace(unsigned(current_),raster->width,raster->height,pose,{x_==10000,y_==10000,width_==10000,height_==10000,angle_==10000});
        const auto x=float((1280-std::int64_t(raster->width))/2),y=float((720-std::int64_t(raster->height))/2);
        layers_[current_]=BackgroundLayer{std::move(raster),x,y,width,height,0};
        request_.reset();ready_image_.reset();loading_=false;
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
    auto previous=layers_[current_^1],current=layers_[current_];
    const auto transform=[&](std::optional<BackgroundLayer>& layer,unsigned index){if(!layer)return;const auto& pose=camera_.pose(index);
        layer->source_x=pose.x;layer->source_y=pose.y;layer->source_width=layer->width;layer->source_height=layer->height;
        layer->scale_x=pose.scale_x;layer->scale_y=pose.scale_y;layer->angle=pose.angle;layer->pivot_x=pose.pivot_x;layer->pivot_y=pose.pivot_y;};
    transform(previous,unsigned(current_^1));transform(current,unsigned(current_));BackgroundFrame result{std::move(previous),std::move(current),loading_?std::nullopt:mask_};
    if(active_ && !loading_){
        result.transition.emplace();auto& transition=*result.transition;transition.current.opacity=layers_[current_]->opacity;
        if(effect_>=11 && effect_<=13){const auto remaining=float(1.-double(elapsed_)/effective_duration_),square=float(double(remaining)*remaining);
            if(effect_!=12)transition.current.transform.scale=1+square;
            if(effect_!=11)transition.previous.transform.scale=2-square;
        }
        if(effect_==14){
            // Cached bounce curve: .5 + 2t - 1.5t^2. Keep its stored
            // binary32 intermediates rather than replacing it with a spring.
            const auto duration=float(effective_duration_),elapsed=float(elapsed_);
            const auto centre=double(duration)*2./3.;const auto centre_squared=float(double(float(centre))*float(centre));
            const auto delta=float(double(elapsed)-centre),delta_squared=float(double(delta)*delta);
            const auto numerator=float(double(centre_squared)-delta_squared),tail=float(double(duration)-float(centre));
            const auto tail_squared=float(double(tail)*tail),ratio=float(double(numerator)/(double(centre_squared)-tail_squared));
            transition.current.transform.scale=float(double(ratio)*.5+.5);
        }
        if(effect_>=16 && effect_<=19){const auto alpha=std::int64_t(elapsed_)*255/effective_duration_,remaining=256-alpha;
            const auto extent=effect_<18?720:1280;const auto distance=extent-remaining*remaining*extent/65536;
            auto& before=transition.previous.transform;auto& after=transition.current.transform;
            if(effect_==16){before.y=float(distance);after.y=float(distance-extent);}
            if(effect_==17){before.y=float(-distance);after.y=float(extent-distance);}
            if(effect_==18){before.x=float(-distance);after.x=float(extent-distance);}
            if(effect_==19){before.x=float(distance);after.x=float(distance-extent);}
            transition.previous.opacity=transition.current.opacity=std::uint8_t(alpha);
        }
    }
    return result;
}
std::vector<std::uint8_t> SceneBackground::checkpoint(const resource::Catalog& resources)const{
    checkpoint::Writer out;out.u32(std::uint32_t(current_));for(auto value:{elapsed_,duration_,effective_duration_,effect_,mask_mode_,x_,y_})out.i32(value);
    const auto real=[&](float value){out.u32(std::bit_cast<std::uint32_t>(value));};
    real(width_);real(height_);real(angle_);for(auto value:{extended_,loading_,active_,picture_})out.boolean(value);out.blob(camera_.checkpoint());
    const auto image=[&](const auto& value){out.blob(image::checkpoint_reference(resources.reference(value)));};
    for(const auto& layer:layers_){out.boolean(bool(layer));if(!layer)continue;image(layer->image);
        for(auto v:{layer->x,layer->y,layer->width,layer->height,layer->angle,layer->pivot_x,layer->pivot_y,layer->source_x,layer->source_y,layer->source_width,layer->source_height,layer->scale_x,layer->scale_y})real(v);
        out.u32(layer->opacity);for(auto v:layer->colour)out.i32(v);}
    out.boolean(bool(request_));if(request_)image(request_);out.boolean(bool(ready_image_));if(ready_image_)image(ready_image_);
    out.boolean(bool(mask_request_));if(mask_request_)image(mask_request_);out.boolean(bool(mask_));if(mask_){image(mask_->image);real(mask_->bias);real(mask_->gain);out.boolean(mask_->flip_x);out.boolean(mask_->flip_y);out.boolean(mask_->invert);}
    return out.seal("LSBG");
}
void SceneBackground::restore(std::span<const std::uint8_t> bytes,resource::Catalog& resources,const Resolve& resolve){
    if(!resolve)throw Error("Missing saved background image resolver");auto in=checkpoint::Reader::open("LSBG",bytes);SceneBackground next(loader_,changing_,mask_loader_,event_loader_);
    next.current_=in.u32();next.elapsed_=in.i32();next.duration_=in.i32();next.effective_duration_=in.i32();next.effect_=in.i32();next.mask_mode_=in.i32();next.x_=in.i32();next.y_=in.i32();
    const auto real=[&]{const auto value=std::bit_cast<float>(in.u32());if(!std::isfinite(value) || std::abs(value)>1.e9f)throw Error("Invalid saved background coordinate");return value;};
    next.width_=real();next.height_=real();next.angle_=real();next.extended_=in.boolean();next.loading_=in.boolean();next.active_=in.boolean();next.picture_=in.boolean();next.camera_.restore(in.blob(65536));
    const auto reference=[&]{return image::restore_reference(in.blob(2048));};
    std::array<std::optional<image::Reference>,2> layer_refs;
    for(unsigned n=0;n<2;++n){if(!in.boolean())continue;layer_refs[n]=reference();auto& layer=next.layers_[n].emplace();
        layer.x=real();layer.y=real();layer.width=real();layer.height=real();layer.angle=real();layer.pivot_x=real();layer.pivot_y=real();layer.source_x=real();layer.source_y=real();layer.source_width=real();layer.source_height=real();layer.scale_x=real();layer.scale_y=real();
        const auto alpha=in.u32();if(alpha>255 || layer.width<=0 || layer.height<=0 || layer.source_width<0 || layer.source_height<0 || layer.scale_x<0 || layer.scale_y<0)throw Error("Invalid saved background layer");layer.opacity=std::uint8_t(alpha);
        for(auto& c:layer.colour){c=in.i32();if(c<0 || c>255)throw Error("Invalid saved background colour");}}
    std::optional<image::Reference> request,ready,mask_request,mask;
    if(in.boolean())request=reference();if(in.boolean())ready=reference();if(in.boolean())mask_request=reference();if(in.boolean()){mask=reference();auto& m=next.mask_.emplace();m.bias=real();m.gain=real();m.flip_x=in.boolean();m.flip_y=in.boolean();m.invert=in.boolean();if(m.gain<=0 || m.gain>32)throw Error("Invalid saved background mask gain");}
    in.finish();const auto effect=next.effect_;const bool masked=effect>=128 && (effect&~0x19ff)==0;
    if(next.current_>1 || next.elapsed_<0 || next.duration_<1 || next.effective_duration_<0 || next.width_<=0 || next.height_<=0 || next.width_>10000 || next.height_>10000 ||
       (effect!=0 && effect!=1 && !(effect>=11 && effect<=14) && !(effect>=16 && effect<=19) && !masked) || (masked && (next.mask_mode_<0 || next.mask_mode_>3)) ||
       (next.loading_ && (!next.active_ || bool(request)==bool(ready) || next.layers_[next.current_])) ||
       (!next.loading_ && (request || ready || mask_request)) || (next.active_ && !next.loading_ && (!next.layers_[next.current_] || next.effective_duration_<=0 || next.elapsed_>=next.effective_duration_)) ||
       (mask && (!masked || next.loading_ || !next.active_)))throw Error("Invalid saved background transition state");
    const auto load=[&](const image::Reference& ref){auto raster=resolve(ref);if(!raster || !raster->width || !raster->height || raster->width>65535 || raster->height>65535 || std::uint64_t(raster->width)*raster->height*4!=raster->rgba.size())throw Error("Saved background image resolver returned invalid pixels");
        if(image::checkpoint_reference(resources.reference(raster))!=image::checkpoint_reference(ref))throw Error("Saved background image resolver returned a different resource");return raster;};
    for(unsigned n=0;n<2;++n)if(layer_refs[n]){auto raster=load(*layer_refs[n]);auto& layer=*next.layers_[n];if(layer.width!=float(raster->width) || layer.height!=float(raster->height))throw Error("Saved background image dimensions changed");layer.image=std::move(raster);}
    if(ready)next.ready_image_=load(*ready);if(mask)next.mask_->image=load(*mask);
    if(request)next.request_=resources.image(*request);if(mask_request)next.mask_request_=resources.image(*mask_request);
    layers_=std::move(next.layers_);camera_=std::move(next.camera_);current_=next.current_;elapsed_=next.elapsed_;duration_=next.duration_;effective_duration_=next.effective_duration_;effect_=next.effect_;mask_mode_=next.mask_mode_;x_=next.x_;y_=next.y_;width_=next.width_;height_=next.height_;angle_=next.angle_;extended_=next.extended_;loading_=next.loading_;active_=next.active_;picture_=next.picture_;
    request_=std::move(next.request_);ready_image_=std::move(next.ready_image_);mask_request_=std::move(next.mask_request_);mask_=std::move(next.mask_);
}
}
