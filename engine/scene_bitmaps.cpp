#include "scene_bitmaps.hpp"
#include <algorithm>
#include <bit>
#include <cmath>

namespace wa2::script {
SceneBitmaps::SceneBitmaps(Loader loader):loader_(std::move(loader)){if(!loader_)throw Error("missing native bitmap loader");sprites_.reserve(slots_.size());}
BitmapSlot& SceneBitmaps::checked(std::int32_t index){if(index<0 || std::size_t(index)>=slots_.size())throw Error("native bitmap index out of range");return slots_[std::size_t(index)];}
const BitmapSlot& SceneBitmaps::slot(std::int32_t index)const{return const_cast<SceneBitmaps*>(this)->checked(index);}
void SceneBitmaps::clear(){slots_={};sprites_.clear();}
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
            item={};item.request=std::move(request);item.layer=value(2);item.independent=value(7)!=0;item.allocated=true;command.continuation=1;
        }
        return CommandResult{!item.request,!item.image,Value{}};
    }
    case 177:case 178:case 182:return std::nullopt; // Animation/text-image loaders have their own native boundaries.
    case 179:count(1);return done(!checked(value(0)).request);
    case 180:count(1);checked(value(0))={};return done();
    case 181:count(1);return done(!checked(value(0)).allocated);
    case 183:count(2);checked(value(0)).visible=value(1)!=0;return done();
    case 184:count(2);if(value(1)<0 || value(1)>255)throw Unsupported("native bitmap drawing layer");checked(value(0)).layer=value(1);return done();
    case 185:{
        count(4);if(value(1)!=1 && value(1)!=3)throw Unsupported("native bitmap blend mode "+std::to_string(value(1)));
        auto& item=checked(value(0));item.blend=value(1);item.duration=std::max(0,int(short_value(3)));item.from=item.alpha;item.target=std::clamp(value(2),0,255);item.elapsed=0;
        if(!item.duration)item.alpha=item.target;return done();
    }
    case 186:count(2);if((value(1)&~3)!=0)throw Unsupported("native bitmap reverse flags");checked(value(0)).flip=std::uint32_t(value(1));return done();
    case 187:{count(4);auto& item=checked(value(0));for(std::size_t n=0;n<3;++n){if(value(n+1)>128)throw Unsupported("native bitmap additive brightness");item.colour[n]=std::max(0,value(n+1));}return done();}
    case 188:{count(3);auto& item=checked(value(0));item.x=short_value(1);item.y=short_value(2);return done();}
    case 189:{count(7);auto& item=checked(value(0));item.x=short_value(1);item.y=short_value(2);item.source_x=short_value(3);item.source_y=short_value(4);
        item.width=item.source_width=short_value(5);item.height=item.source_height=short_value(6);return done();}
    case 190:{count(5);auto& item=checked(value(0));item.x=short_value(1);item.y=short_value(2);item.width=short_value(3);item.height=short_value(4);return done();}
    case 191:{count(4);auto& item=checked(value(0));const auto scale=command.arguments[3].real();if(!std::isfinite(scale) || scale<0 || scale>100)throw Unsupported("native bitmap zoom");
        item.pivot_x=short_value(1);item.pivot_y=short_value(2);item.scale=scale;return done();}
    case 192:{count(4);auto& item=checked(value(0));item.pivot_x=short_value(1);item.pivot_y=short_value(2);item.angle=float(double(value(3))*360/4096);return done();}
    default:return std::nullopt;
    }
}
void SceneBitmaps::update(const std::function<std::int32_t(std::int32_t)>& adjust){
    for(auto& item:slots_){
        if(item.request){auto image=item.request->get();if(image){item.image=std::move(image);item.request.reset();item.width=item.source_width=float(item.image->width);item.height=item.source_height=float(item.image->height);}}
        if(item.duration){const auto duration=adjust?adjust(item.duration):item.duration;
            if(duration<=0 || item.elapsed>=duration){item.alpha=item.target;item.duration=0;}
            else{item.alpha=std::int32_t((std::int64_t(duration-item.elapsed)*item.from+std::int64_t(item.elapsed)*item.target)/duration);++item.elapsed;}}
    }
}
std::span<const image::Sprite> SceneBitmaps::sprites(std::int32_t first_layer,std::int32_t last_layer,int independent){
    sprites_.clear();std::array<std::size_t,32> order{};for(std::size_t n=0;n<order.size();++n)order[n]=n;
    std::sort(order.begin(),order.end(),[&](auto a,auto b){return slots_[a].layer==slots_[b].layer?a<b:slots_[a].layer<slots_[b].layer;});
    for(auto index:order){const auto& item=slots_[index];if(!item.allocated || !item.visible || !item.image || !item.alpha || item.layer<first_layer || item.layer>last_layer ||
        (independent!=-1 && item.independent!=bool(independent)))continue;
        const auto x=item.pivot_x+(item.x-item.pivot_x)*item.scale,y=item.pivot_y+(item.y-item.pivot_y)*item.scale;
        sprites_.push_back({item.image,x,y,item.width*item.scale,item.height*item.scale,item.source_x,item.source_y,item.source_width,item.source_height,
            std::uint8_t(std::clamp(item.colour[0]*2,0,255)),std::uint8_t(std::clamp(item.colour[1]*2,0,255)),std::uint8_t(std::clamp(item.colour[2]*2,0,255)),
            std::uint8_t(item.alpha),item.angle,item.pivot_x,item.pivot_y,item.flip,item.blend==3?image::SpriteBlend::add:image::SpriteBlend::alpha});
        sprites_.back().layer=item.layer+(item.independent?0x1035:0);sprites_.back().order=0x11+std::int32_t(index);
    }
    return sprites_;
}
}
