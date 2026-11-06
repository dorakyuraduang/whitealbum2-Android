#include "scene_bitmaps.hpp"
#include <algorithm>
#include <bit>
#include <cmath>

namespace wa2::script {
namespace {
image::SpriteBlend sprite_blend(std::int32_t mode){switch(mode){case 1:return image::SpriteBlend::alpha;case 3:return image::SpriteBlend::add;
    case 4:return image::SpriteBlend::screen;case 6:return image::SpriteBlend::multiply;default:throw Unsupported("native bitmap blend mode "+std::to_string(mode));}}
}
SceneBitmaps::SceneBitmaps(Loader loader,AnimationLoader animations):loader_(std::move(loader)),animation_loader_(std::move(animations)){if(!loader_)throw Error("missing native bitmap loader");sprites_.reserve(slots_.size()*2);retired_.reserve(slots_.size());}
BitmapSlot& SceneBitmaps::checked(std::int32_t index){if(index<0 || std::size_t(index)>=slots_.size())throw Error("native bitmap index out of range");return slots_[std::size_t(index)];}
const BitmapSlot& SceneBitmaps::slot(std::int32_t index)const{return const_cast<SceneBitmaps*>(this)->checked(index);}
void SceneBitmaps::clear(){slots_={};retired_.clear();sprites_.clear();}
bool SceneBitmaps::loading()const noexcept{for(const auto& slot:slots_)if(slot.loading())return true;for(const auto& slot:retired_)if(slot.loading())return true;return false;}
std::optional<CommandResult> SceneBitmaps::dispatch(Command& command){
    if(command.index<176 || command.index>192)return std::nullopt;
    const auto count=[&](std::size_t n){if(command.arguments.size()!=n)throw Error("invalid native bitmap parameters");};
    const auto value=[&](std::size_t n){return command.arguments[n].integer();};
    const auto short_value=[&](std::size_t n){return std::bit_cast<std::int16_t>(std::uint16_t(value(n)));};
    const auto done=[](int result=0){return CommandResult{true,false,Value(result)};};
    switch(command.index){
    case 176:{
        count(8);auto& item=checked(value(0));
        if(!command.continuation){
            if(!command.arguments[4].text().empty() || value(5)!=256)throw Unsupported("native bitmap custom palette conversion");
            if(value(3)<0 || value(3)>2)throw Unsupported("native bitmap image format "+std::to_string(value(3)));
            if(value(2)<0 || value(2)>255 || (value(7)!=0 && value(7)!=1))throw Unsupported("native bitmap drawing layer");
            auto request=loader_(command.arguments[1].text(),command.arguments[6].text(),value(3));if(!request)throw Error("native bitmap loader returned no request");
            if(item.retiring){if(retired_.size()>=slots_.size())throw Error("Native retired bitmap bank is full");retired_.push_back(item);}
            item={};item.request=std::move(request);item.layer=value(2);item.order_index=value(0);item.independent=value(7)!=0;item.allocated=true;command.continuation=1;
        }
        return CommandResult{!item.request,!item.image,Value{}};
    }
    case 177:{
        count(7);auto& item=checked(value(0));
        if(!command.continuation){
            if(!animation_loader_)throw Unsupported("Missing native bitmap animation loader");
            if(!command.arguments[3].text().empty() || value(4)!=256)throw Unsupported("Native animated bitmap palette conversion");
            if(value(2)<0 || value(2)>255 || (value(6)!=0 && value(6)!=1))throw Unsupported("Native animated bitmap drawing layer");
            auto data=animation_loader_(command.arguments[1].text(),command.arguments[5].text());if(!data)throw Error("Native animation loader returned no definition");
            if(data->textures().size()>8)throw Unsupported("Native animated bitmap has more than eight concurrent textures");
            BitmapSlot next;next.animation=std::move(data);next.player.emplace(next.animation);next.width=1280;next.height=720;next.layer=value(2);next.order_index=value(0);next.independent=value(6)!=0;next.allocated=true;
            for(const auto& name:next.animation->textures()){auto request=loader_(name,command.arguments[5].text(),1);if(!request)throw Error("Native animation texture loader returned no request");next.texture_requests.push_back(std::move(request));}
            next.textures.resize(next.texture_requests.size());if(item.retiring){if(retired_.size()>=slots_.size())throw Error("Native retired bitmap bank is full");retired_.push_back(item);}item=std::move(next);command.continuation=1;
        }
        return CommandResult{!item.loading(),item.loading(),Value{}};
    }
    case 178:case 182:return std::nullopt; // AVI bitmaps/animation playback control remain explicit boundaries.
    case 179:count(1);return done(!checked(value(0)).loading());
    case 180:count(1);checked(value(0))={};return done();
    case 181:count(1);return done(!checked(value(0)).allocated);
    case 183:count(2);checked(value(0)).visible=value(1)!=0;return done();
    case 184:count(2);if(value(1)<0 || value(1)>255)throw Unsupported("native bitmap drawing layer");checked(value(0)).layer=value(1);return done();
    case 185:{
        count(4);sprite_blend(value(1));
        auto& item=checked(value(0));item.blend=value(1);item.duration=std::max(0,int(short_value(3)));item.from=item.alpha;item.target=std::clamp(value(2),0,255);item.elapsed=0;
        if(!item.duration)item.alpha=item.target;return done();
    }
    case 186:count(2);if((value(1)&~3)!=0)throw Unsupported("native bitmap reverse flags");checked(value(0)).flip=std::uint32_t(value(1));return done();
    case 187:{count(4);auto& item=checked(value(0));for(std::size_t n=0;n<3;++n){if(value(n+1)>128)throw Unsupported("native bitmap additive brightness");item.colour[n]=std::max(0,value(n+1));}return done();}
    case 188:{count(3);auto& item=checked(value(0));item.pivot_x=item.x=short_value(1);item.pivot_y=item.y=short_value(2);return done();}
    case 189:{count(7);auto& item=checked(value(0));if(item.animation)throw Unsupported("Native animated bitmap destination rectangles are not implemented");item.x=short_value(1);item.y=short_value(2);item.source_x=short_value(3);item.source_y=short_value(4);
        item.pivot_x=item.x;item.pivot_y=item.y;item.width=item.source_width=short_value(5);item.height=item.source_height=short_value(6);return done();}
    case 190:{count(5);auto& item=checked(value(0));if(item.animation)throw Unsupported("Native animated bitmap destination rectangles are not implemented");item.pivot_x=item.x=short_value(1);item.pivot_y=item.y=short_value(2);item.width=short_value(3);item.height=short_value(4);return done();}
    case 191:{count(4);auto& item=checked(value(0));const auto scale=command.arguments[3].real();if(!std::isfinite(scale) || scale<0 || scale>100)throw Unsupported("native bitmap zoom");
        item.pivot_x=short_value(1);item.pivot_y=short_value(2);item.scale=scale;return done();}
    case 192:{count(4);auto& item=checked(value(0));item.pivot_x=short_value(1);item.pivot_y=short_value(2);item.angle=float(double(value(3))*360/4096);return done();}
    default:return std::nullopt;
    }
}
void SceneBitmaps::update(const std::function<std::int32_t(std::int32_t)>& adjust){
    const auto update=[&](auto& items){for(auto& item:items){
        if(item.request){auto image=item.request->get();if(image){item.image=std::move(image);item.request.reset();item.width=item.source_width=float(item.image->width);item.height=item.source_height=float(item.image->height);}}
        for(std::size_t n=0;n<item.texture_requests.size();++n)if(item.texture_requests[n])if(auto texture=item.texture_requests[n]->get()){item.textures[n]=std::move(texture);item.texture_requests[n].reset();}
        if(item.player && !item.loading()){
            if(!item.animation_validated){for(const auto& frame:item.animation->frames())for(const auto& piece:frame.pieces){const auto& texture=item.textures.at(piece.texture);
                    if(unsigned(piece.source_x)+unsigned(piece.width)>texture->width || unsigned(piece.source_y)+unsigned(piece.height)>texture->height)throw Error("Animated bitmap crop exceeds its texture");}item.animation_validated=true;}
            item.player->tick();
        }
        if(item.duration){const auto duration=adjust?adjust(item.duration):item.duration;
            if(duration<=0 || item.elapsed>=duration){item.alpha=item.target;item.duration=0;}
            else{item.alpha=std::int32_t((std::int64_t(duration-item.elapsed)*item.from+std::int64_t(item.elapsed)*item.target)/duration);++item.elapsed;}}
    }};update(slots_);update(retired_);
}
std::span<const image::Sprite> SceneBitmaps::sprites(std::int32_t first_layer,std::int32_t last_layer,int independent){
    sprites_.clear();const auto append_bank=[&](const auto& bank){std::array<std::size_t,32> order{};for(std::size_t n=0;n<bank.size();++n)order[n]=n;
    std::sort(order.begin(),order.begin()+bank.size(),[&](auto a,auto b){return bank[a].layer==bank[b].layer?a<b:bank[a].layer<bank[b].layer;});
    for(std::size_t n=0;n<bank.size();++n){const auto index=order[n];const auto& item=bank[index];if(!item.allocated || !item.visible || (!item.image && !item.animation) || !image::contributes(sprite_blend(item.blend),std::uint8_t(item.alpha)) || item.layer<first_layer || item.layer>last_layer ||
        (independent!=-1 && item.independent!=bool(independent)))continue;
        if(item.animation){
            if(item.loading() || !item.player || !item.player->frame())continue;
            for(const auto& piece:item.animation->frames()[*item.player->frame()].pieces){if(piece.empty())continue;
                const auto x=item.pivot_x+(item.x+piece.x-item.pivot_x)*item.scale,y=item.pivot_y+(item.y+piece.y-item.pivot_y)*item.scale;
                sprites_.push_back({item.textures.at(piece.texture),x,y,piece.width*item.scale,piece.height*item.scale,float(piece.source_x),float(piece.source_y),float(piece.width),float(piece.height),
                    std::uint8_t(std::clamp(item.colour[0]*2,0,255)),std::uint8_t(std::clamp(item.colour[1]*2,0,255)),std::uint8_t(std::clamp(item.colour[2]*2,0,255)),
                    std::uint8_t(item.alpha),item.angle,item.pivot_x,item.pivot_y,item.flip,sprite_blend(item.blend)});
                sprites_.back().layer=item.layer+(item.independent?0x1035:0);sprites_.back().order=0x11+item.order_index;
                sprites_.back().world=item.retiring?image::SpriteWorld::previous:image::SpriteWorld::current;
            }continue;
        }
        const auto x=item.pivot_x+(item.x-item.pivot_x)*item.scale,y=item.pivot_y+(item.y-item.pivot_y)*item.scale;
        sprites_.push_back({item.image,x,y,item.width*item.scale,item.height*item.scale,item.source_x,item.source_y,item.source_width,item.source_height,
            std::uint8_t(std::clamp(item.colour[0]*2,0,255)),std::uint8_t(std::clamp(item.colour[1]*2,0,255)),std::uint8_t(std::clamp(item.colour[2]*2,0,255)),
            std::uint8_t(item.alpha),item.angle,item.pivot_x,item.pivot_y,item.flip,sprite_blend(item.blend)});
        sprites_.back().layer=item.layer+(item.independent?0x1035:0);sprites_.back().order=0x11+item.order_index;
        sprites_.back().world=item.retiring?image::SpriteWorld::previous:image::SpriteWorld::current;
    }};append_bank(retired_);append_bank(slots_);
    std::sort(sprites_.begin(),sprites_.end(),[](const auto& a,const auto& b){if(a.layer!=b.layer)return a.layer<b.layer;if(a.order!=b.order)return a.order<b.order;return a.world<b.world;});
    return sprites_;
}
}
