#include "scene_weather.hpp"
#include "resource_catalog.hpp"
#include "checkpoint_codec.hpp"
#include <algorithm>
#include <bit>
#include <cmath>

namespace wa2::script {
std::vector<std::uint8_t> SceneWeather::checkpoint(const resource::Catalog& resources)const{
    checkpoint::Writer out;out.u32(std::uint32_t(extent_));out.u32(frame_);for(auto value:{type_,flags_,priority_,scope_,density_,target_x_,target_y_,amplitude_,target_amplitude_})out.i32(value);
    for(auto value:{speed_x_,speed_y_})out.u32(std::bit_cast<std::uint32_t>(value));out.boolean(enabled_);out.boolean(prefill_);out.boolean(stopping_);
    const auto pixels=[&](const auto& request,const auto& image){out.boolean(bool(request));if(request)out.blob(image::checkpoint_reference(resources.reference(request)));out.boolean(bool(image));if(image)out.blob(image::checkpoint_image(resources,image));};
    pixels(request_,image_);for(unsigned n=0;n<3;++n)pixels(sakura_requests_[n],sakura_images_[n]);
    out.boolean(bool(animation_));if(animation_){if(!animation_player_ || animation_textures_.size()!=animation_requests_.size())throw Error("Invalid native rain playback state");out.blob(animation_player_->checkpoint());out.u32(std::uint32_t(animation_textures_.size()));for(unsigned n=0;n<animation_textures_.size();++n)pixels(animation_requests_[n],animation_textures_[n]);}
    for(const auto& particle:particles_){out.boolean(particle.active);out.i32(particle.kind);out.i32(particle.phase);for(auto value:{particle.angle,particle.radius,particle.rate,particle.rotation,particle.scale,particle.growth,particle.velocity})out.u32(std::bit_cast<std::uint32_t>(value));}
    std::array<std::shared_ptr<const image::Raster>,13> textures; textures[0]=image_;for(unsigned n=0;n<3;++n)textures[n+1]=sakura_images_[n];if(sakura_atlas_)textures[4]=sakura_atlas_->image;for(unsigned n=0;n<animation_textures_.size();++n)textures[n+5]=animation_textures_[n];
    out.u32(std::uint32_t(sprites_.size()));for(const auto& sprite:sprites_){const auto selected=std::find(textures.begin(),textures.end(),sprite.image);if(!sprite.image || selected==textures.end())throw Error("Weather sprite is not owned by the current effect");out.u32(std::uint32_t(selected-textures.begin()));
        for(auto value:{sprite.x,sprite.y,sprite.width,sprite.height,sprite.source_x,sprite.source_y,sprite.source_width,sprite.source_height,sprite.angle,sprite.pivot_x,sprite.pivot_y})out.u32(std::bit_cast<std::uint32_t>(value));out.u32(unsigned(sprite.red)|(unsigned(sprite.green)<<8)|(unsigned(sprite.blue)<<16)|(unsigned(sprite.alpha)<<24));out.u32(sprite.flip);out.i32(int(sprite.blend));out.i32(sprite.layer);out.i32(sprite.order);out.i32(int(sprite.world));}
    return out.seal("LSWE");
}
void SceneWeather::restore(std::span<const std::uint8_t> bytes,resource::Catalog& resources,const image::Resolve& resolve){
    if(!resolve)throw Error("Missing saved weather image resolver");auto in=checkpoint::Reader::open("LSWE",bytes);SceneWeather next(loader_,random_,animation_loader_);next.extent_=in.u32();next.frame_=in.u32();
    for(auto* value:{&next.type_,&next.flags_,&next.priority_,&next.scope_,&next.density_,&next.target_x_,&next.target_y_,&next.amplitude_,&next.target_amplitude_})*value=in.i32();next.speed_x_=std::bit_cast<float>(in.u32());next.speed_y_=std::bit_cast<float>(in.u32());next.enabled_=in.boolean();next.prefill_=in.boolean();next.stopping_=in.boolean();
    if(next.extent_>400 || (next.type_!=0 && next.type_!=1 && next.type_!=3 && next.type_!=4 && next.type_!=6) || (next.flags_&~0x600) || next.priority_<0 || next.priority_>2 || next.scope_<0 || next.scope_>2 || next.density_<0 || next.density_>400 || !std::isfinite(next.speed_x_) || !std::isfinite(next.speed_y_))throw Error("Invalid saved weather parameters");
    const auto pixels=[&](auto& request,auto& image){if(in.boolean())request=resources.image(image::restore_reference(in.blob(2048)));if(in.boolean())image=image::restore_image(in.blob(4096),resources,resolve);if(request && image)throw Error("Saved weather image is both pending and loaded");};
    pixels(next.request_,next.image_);for(unsigned n=0;n<3;++n)pixels(next.sakura_requests_[n],next.sakura_images_[n]);
    if(in.boolean()){
        if(!animation_loader_)throw Unsupported("Missing saved rain animation loader");next.animation_=animation_loader_("\x89\x4a" "640x360.ani");if(!next.animation_ || next.animation_->textures().size()>8)throw Error("Invalid saved rain definition");next.animation_player_.emplace(next.animation_);next.animation_player_->restore(in.blob(4096));const auto count=in.u32();if(count!=next.animation_->textures().size())throw Error("Saved rain texture count changed");next.animation_requests_.resize(count);next.animation_textures_.resize(count);
        for(unsigned n=0;n<count;++n){pixels(next.animation_requests_[n],next.animation_textures_[n]);if(!next.animation_requests_[n] && !next.animation_textures_[n])throw Error("Missing saved rain texture");}
    }
    unsigned live{};for(auto& particle:next.particles_){particle.active=in.boolean();const auto kind=in.i32(),phase=in.i32();if(kind<0 || kind>255 || phase<-32768 || phase>32767)throw Error("Invalid saved weather particle identity");particle.kind=std::int16_t(kind);particle.phase=std::int16_t(phase);
        for(auto* value:{&particle.angle,&particle.radius,&particle.rate,&particle.rotation,&particle.scale,&particle.growth,&particle.velocity}){*value=std::bit_cast<float>(in.u32());if(!std::isfinite(*value))throw Error("Non-finite saved weather particle");}
        if(particle.active){++live;if((next.type_==0) || (next.type_==1 && (kind>2 || phase<0 || phase>=(kind==1?2304:768))) || ((next.type_==4 || next.type_==6) && kind>3))throw Error("Saved particle does not match its weather type");}}
    if(live!=next.extent_)throw Error("Saved weather population is inconsistent");
    const bool ordinary=next.request_ || next.image_,sakura=std::any_of(next.sakura_requests_.begin(),next.sakura_requests_.end(),[](const auto& value){return bool(value);}) || std::any_of(next.sakura_images_.begin(),next.sakura_images_.end(),[](const auto& value){return bool(value);});
    if(!next.enabled_){if(live || ordinary || sakura || next.animation_)throw Error("Disabled saved weather owns active resources");}
    else if(next.type_==0){if(!next.animation_ || ordinary || sakura || live)throw Error("Inconsistent saved rain state");}
    else if(next.type_==1){if(ordinary || next.animation_)throw Error("Inconsistent saved sakura state");for(unsigned n=0;n<3;++n)if(!next.sakura_requests_[n] && !next.sakura_images_[n])throw Error("Missing saved sakura texture");}
    else if(!ordinary || sakura || next.animation_)throw Error("Inconsistent saved weather image state");
    if(next.image_ && (next.image_->width<32 || next.image_->height<64))throw Error("Saved weather atlas is too small");
    if(next.type_==1 && next.enabled_ && !next.loading()){
        constexpr unsigned cells[]{128,32,16};for(unsigned n=0;n<3;++n)if(next.sakura_images_[n]->width<12*cells[n] || next.sakura_images_[n]->height<2*cells[n])throw Error("Saved sakura atlas is too small");next.sakura_atlas_=image::make_sprite_atlas(next.sakura_images_);next.animation_validated_=true;
    }
    if(next.animation_ && !next.loading()){
        for(const auto& frame:next.animation_->frames())for(const auto& piece:frame.pieces){const auto& texture=next.animation_textures_.at(piece.texture);if(unsigned(piece.source_x)+unsigned(piece.width)>texture->width || unsigned(piece.source_y)+unsigned(piece.height)>texture->height)throw Error("Saved rain crop exceeds its texture");}next.animation_validated_=true;
    }
    std::array<std::shared_ptr<const image::Raster>,13> textures;textures[0]=next.image_;for(unsigned n=0;n<3;++n)textures[n+1]=next.sakura_images_[n];if(next.sakura_atlas_)textures[4]=next.sakura_atlas_->image;for(unsigned n=0;n<next.animation_textures_.size();++n)textures[n+5]=next.animation_textures_[n];
    const auto count=in.u32();if(count>400 || (count && (!next.enabled_ || next.loading())) || (next.type_==0 && count>256))throw Error("Invalid saved weather sprite count");next.sprites_.reserve(400);
    for(unsigned n=0;n<count;++n){image::Sprite sprite;const auto selected=in.u32();if(selected>=textures.size() || !textures[selected])throw Error("Invalid saved weather sprite texture");sprite.image=textures[selected];for(auto* value:{&sprite.x,&sprite.y,&sprite.width,&sprite.height,&sprite.source_x,&sprite.source_y,&sprite.source_width,&sprite.source_height,&sprite.angle,&sprite.pivot_x,&sprite.pivot_y}){*value=std::bit_cast<float>(in.u32());if(!std::isfinite(*value))throw Error("Non-finite saved weather sprite");}
        const auto colour=in.u32();sprite.red=std::uint8_t(colour);sprite.green=std::uint8_t(colour>>8);sprite.blue=std::uint8_t(colour>>16);sprite.alpha=std::uint8_t(colour>>24);sprite.flip=in.u32();const auto blend=in.i32();sprite.layer=in.i32();sprite.order=in.i32();const auto world=in.i32();
        if(sprite.source_x<0 || sprite.source_y<0 || sprite.source_width<=0 || sprite.source_height<=0 || double(sprite.source_x)+sprite.source_width>sprite.image->width || double(sprite.source_y)+sprite.source_height>sprite.image->height || sprite.flip || blend!=int(image::SpriteBlend::alpha) || world!=int(image::SpriteWorld::both))throw Error("Invalid saved weather sprite geometry");
        if((next.type_==0 && (sprite.layer!=0x1043 || sprite.order!=0x51)) || (next.type_!=0 && (sprite.order<0x99 || sprite.order>=0x99+400 || (sprite.layer!=1 && sprite.layer!=10 && sprite.layer!=0x1034 && sprite.layer!=0x1039 && sprite.layer!=0x1043))))throw Error("Invalid saved weather sprite order");next.sprites_.push_back(std::move(sprite));
    }
    in.finish();*this=std::move(next);
}
}
