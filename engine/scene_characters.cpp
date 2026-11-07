#include "scene_characters.hpp"
#include "resource_catalog.hpp"
#include "checkpoint_codec.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>

namespace wa2::script {
namespace {
#include "character_constants.inc"
std::int32_t short_integer(std::int32_t n){return std::bit_cast<std::int16_t>(std::uint16_t(n));}
bool effect_supported(std::int32_t effect){return effect>=-2 && (effect<=3 || effect==6 || effect==7);}
}
SceneCharacters::SceneCharacters(Loader loader):loader_(std::move(loader)){
    if(!loader_)throw Error("missing native character loader");sprites_.reserve(32);retired_.reserve(8);
}
SceneCharacters::Slot* SceneCharacters::find(std::int32_t id){for(auto& item:slots_)if(item.id==id)return &item;return nullptr;}
const SceneCharacters::Slot* SceneCharacters::find(std::int32_t id)const{return const_cast<SceneCharacters*>(this)->find(id);}
std::size_t SceneCharacters::count()const noexcept{std::size_t n=0;for(const auto& item:slots_)n+=item.id!=-1;return n;}
bool SceneCharacters::busy(std::int32_t id)const{auto* item=find(id);return item && (item->request || item->changing);}
bool SceneCharacters::loading()const noexcept{for(const auto& slot:slots_)if(slot.request)return true;for(const auto& slot:retired_)if(slot.request)return true;return false;}
void SceneCharacters::clear(){slots_={};retired_.clear();sprites_.clear();}
void SceneCharacters::retire_background(){for(auto& item:slots_)if(item.id!=-1)item.retiring=true;}
void SceneCharacters::clear_retired(){for(auto& item:slots_)if(item.retiring)item={};retired_.clear();}
void SceneCharacters::set(std::int32_t id,std::int32_t number,const Value& position,std::int32_t layer,
                          std::int32_t effect,std::int32_t duration,std::int32_t alpha,std::int32_t brightness){
    id=short_integer(id);
    if(id<0 || std::size_t(id)>=character_prefixes.size() || number<0 || number>999999)throw Error("native character identity out of range");
    if(!effect_supported(effect))throw Unsupported("native character transition "+std::to_string(effect));
    if(layer<0 || layer>255 || brightness<0 || brightness>128)throw Unsupported("native character drawing parameters");
    float offset;
    if(position.type()==Type::real){offset=position.real();if(!std::isfinite(offset))throw Error("invalid native character position");}
    else {const auto index=short_integer(position.integer());if(index<0 || std::size_t(index)>=character_positions.size())throw Error("native character placement out of range");offset=float(character_positions[index]);}
    if(effect<0){effect=0;duration=1;}
    auto* item=find(id);
    if(!item){for(auto& candidate:slots_)if(candidate.id==-1){item=&candidate;break;}}
    if(!item)throw Error("more than eight active native characters");
    Pose target{nullptr,number,layer,std::clamp(short_integer(alpha),0,255),brightness,offset};
    target.style_generation=style_generation_;
    const bool same_image=item->id==id && item->current.number==number && item->current.style_generation==style_generation_;
    if(same_image){target.image=item->current.image;if(target==item->current && !item->retiring)return;}
    std::shared_ptr<image::ImageRequest> request;
    if(!same_image){std::array<char,32> name{};std::snprintf(name.data(),name.size(),"%s%06d.tga",character_prefixes[id].data(),int(number));
        request=loader_(name.data());if(!request)throw Error("native character loader returned no request");}
    if(item->retiring){if(retired_.size()>=slots_.size())throw Error("Native retired character bank is full");retired_.push_back(*item);}
    item->previous=item->current;item->current=std::move(target);item->request=std::move(request);item->id=id;
    item->order_index=std::int32_t(item-slots_.data());
    item->effect=effect;item->duration=short_integer(duration);item->elapsed=0;item->progress=0;item->changing=true;item->removing=false;item->retiring=false;
}
void SceneCharacters::remove(std::int32_t id,std::int32_t effect,std::int32_t duration){
    if(!effect_supported(effect))throw Unsupported("native character removal "+std::to_string(effect));
    if(auto* item=find(short_integer(id))){item->request.reset();item->previous={};item->effect=effect<0?0:effect;
        item->duration=effect<0?1:short_integer(duration);item->elapsed=0;item->progress=0;item->changing=true;item->removing=true;}
}
std::optional<CommandResult> SceneCharacters::dispatch(Command& command){
    if(command.index<154 || command.index>157)return std::nullopt;
    const auto count=[&](std::size_t n){if(command.arguments.size()!=n)throw Error("invalid native character parameters");};
    const auto v=[&](std::size_t n){return command.arguments[n].integer();};
    if(command.index==154 || command.index==155){
        const bool wait=command.index==154;count(wait?8:6);
        if(!command.continuation){set(v(0),v(1),command.arguments[2],v(3),wait?v(4):3,wait?v(5):0,wait?v(6):v(4),wait?v(7):v(5));command.continuation=1;}
        const auto* item=find(short_integer(v(0)));const bool ready=!item || (!item->request && (!wait || v(4)==3 || !item->changing));
        return CommandResult{ready,!ready,Value{}};
    }
    count(command.index==156?3:1);
    if(!command.continuation){remove(v(0),command.index==156?v(1):3,command.index==156?v(2):0);command.continuation=1;}
    const bool complete=command.index==157 || v(1)==3 || !busy(short_integer(v(0)));
    return CommandResult{complete,!complete,Value{}};
}
void SceneCharacters::update(const std::function<std::int32_t(std::int32_t)>& adjust){
    const auto update=[&](auto& items){for(auto& item:items){if(item.id==-1)continue;
        if(item.request){auto image=item.request->get();if(!image)continue;item.current.image=std::move(image);item.request.reset();}
        if(!item.changing)continue;
        const auto duration=adjust?adjust(item.duration):item.duration;
        ++item.elapsed;item.progress=duration<=0?1.f:std::min(1.f,float(item.elapsed)/float(duration));
        if(item.progress>=1){
            if(item.removing)item={};else{item.elapsed=std::max(duration,0);item.changing=false;item.previous={};}
        }
    }};update(slots_);update(retired_);
}
std::span<const image::Sprite> SceneCharacters::sprites(){
    sprites_.clear();const auto append_bank=[&](const auto& bank){for(std::size_t n=0;n<bank.size();++n){const auto& item=bank[n];if(item.id==-1)continue;
        const auto t=item.changing?item.progress:1.f;
        const auto progress=item.removing?1-t:t;const auto ease=1-(1-progress)*(1-progress);
        const auto centre=[](const Pose& pose){return pose.image?float((1280-int(pose.image->width))/2)+pose.position:0.f;};
        const bool replacement=bool(item.previous.image) && (item.previous.number!=item.current.number || item.previous.style_generation!=item.current.style_generation);
        auto append=[&](const Pose& pose,bool previous){if(!pose.image)return;
            float x=centre(pose),y=float(720-int(pose.image->height)),scale=1,alpha=float(pose.alpha);
            if(previous)alpha*=1-t;
            else if(item.changing){
                if(item.effect==1)x=(1-ease)*1280+ease*x;
                else if(item.effect==2)x=ease*x-(1-ease)*768;
                else if(item.previous.image && !item.removing && item.effect!=0 && item.effect!=3)x=centre(item.previous)*(1-t)+x*t;
                if(!item.previous.image || item.removing || replacement)alpha*=progress;
                else alpha=float(item.previous.alpha)*(1-t)+alpha*t;
                if(item.effect==6)scale=1-(1-progress)*.25f;
                else if(item.effect==7)scale=1+(1-progress)*.5f;
            }
            const auto width=float(pose.image->width),height=float(pose.image->height);const auto pivot_x=x+float(int(pose.image->width)/2);
            const auto pivot_y=item.effect==7?float(int(pose.image->height)/2):720.f;
            x=pivot_x+(x-pivot_x)*scale;y=pivot_y+(y-pivot_y)*scale;
            const auto brightness=std::uint8_t(std::min(pose.brightness*2,255));
            sprites_.push_back({pose.image,x,y,width*scale,height*scale,0,0,width,height,brightness,brightness,brightness,
                std::uint8_t(std::clamp(int(std::nearbyint(alpha)),0,255))});
            sprites_.back().layer=2+pose.layer;sprites_.back().order=0x379+item.order_index*2+(previous?0:1);
            sprites_.back().world=item.retiring?image::SpriteWorld::previous:image::SpriteWorld::both;
        };
        if(item.changing && replacement)append(item.previous,true);append(item.current,false);
    }};append_bank(retired_);append_bank(slots_);return sprites_;
}
std::vector<std::uint8_t> SceneCharacters::checkpoint(const resource::Catalog& resources)const{
    checkpoint::Writer out;out.u64(style_generation_);out.u32(std::uint32_t(retired_.size()));
    const auto pose=[&](const Pose& value){out.i32(value.number);out.i32(value.layer);out.i32(value.alpha);out.i32(value.brightness);out.u32(std::bit_cast<std::uint32_t>(value.position));out.u64(value.style_generation);out.boolean(bool(value.image));if(value.image)out.blob(image::checkpoint_image(resources,value.image));};
    const auto slot=[&](const Slot& value){for(auto v:{value.id,value.effect,value.duration,value.elapsed,value.order_index})out.i32(v);out.u32(std::bit_cast<std::uint32_t>(value.progress));out.boolean(value.changing);out.boolean(value.removing);out.boolean(value.retiring);pose(value.current);pose(value.previous);out.boolean(bool(value.request));if(value.request)out.blob(image::checkpoint_reference(resources.reference(value.request)));};
    for(const auto& item:slots_)slot(item);for(const auto& item:retired_)slot(item);return out.seal("LSAC");
}
void SceneCharacters::restore(std::span<const std::uint8_t> bytes,resource::Catalog& resources,const image::Resolve& resolve){
    if(!resolve)throw Error("Missing saved character image resolver");auto in=checkpoint::Reader::open("LSAC",bytes);const auto generation=in.u64();const auto retired_count=in.u32();if(retired_count>8)throw Error("Saved retired character count exceeds eight");
    const auto pose=[&]{Pose value;value.number=in.i32();value.layer=in.i32();value.alpha=in.i32();value.brightness=in.i32();value.position=std::bit_cast<float>(in.u32());value.style_generation=in.u64();
        if(value.number<0 || value.number>999999 || value.layer<0 || value.layer>255 || value.alpha<0 || value.alpha>255 || value.brightness<0 || value.brightness>128 || !std::isfinite(value.position) || std::abs(value.position)>1.e9f || value.style_generation>generation)throw Error("Invalid saved character pose");
        if(in.boolean())value.image=image::restore_image(in.blob(4096),resources,resolve);return value;};
    const auto slot=[&]{Slot value;value.id=in.i32();value.effect=in.i32();value.duration=in.i32();value.elapsed=in.i32();value.order_index=in.i32();value.progress=std::bit_cast<float>(in.u32());value.changing=in.boolean();value.removing=in.boolean();value.retiring=in.boolean();
        if(value.id< -1 || (value.id>=0 && std::size_t(value.id)>=character_prefixes.size()) || value.effect<0 || !effect_supported(value.effect) || value.duration<-32768 || value.duration>32767 || value.elapsed<0 || value.order_index<0 || value.order_index>7 || !std::isfinite(value.progress) || value.progress<0 || value.progress>1)throw Error("Invalid saved character transition");
        value.current=pose();value.previous=pose();std::optional<image::Reference> request;if(in.boolean())request=image::restore_reference(in.blob(2048));
        if((value.id==-1 && (value.current.image || value.previous.image || request || value.changing || value.removing || value.retiring)) || (request && (value.current.image || !value.changing)) || (value.removing && !value.changing) || (value.id!=-1 && !value.current.image && !request && !value.removing))throw Error("Inconsistent saved character slot");
        if(request)value.request=resources.image(*request);return value;};
    std::array<Slot,8> next;std::array<bool,character_prefixes.size()> seen{};
    for(unsigned n=0;n<next.size();++n){next[n]=slot();if(next[n].id>=0){if(seen[std::size_t(next[n].id)] || next[n].order_index!=std::int32_t(n))throw Error("Duplicate/misordered saved character slot");seen[std::size_t(next[n].id)]=true;}}
    std::vector<Slot> retired;retired.reserve(8);for(unsigned n=0;n<retired_count;++n)retired.push_back(slot());in.finish();
    slots_=std::move(next);retired_=std::move(retired);style_generation_=generation;sprites_.clear();
}
void SceneCharacters::prepare_restored_assets(resource::Catalog& resources,const image::Resolve& resolve){
    std::vector<std::pair<Slot*,std::shared_ptr<const image::Raster>>> ready;ready.reserve(16);
    const auto prepare=[&](auto& slots){for(auto& slot:slots)if(slot.request)ready.emplace_back(&slot,image::resolve_reference(resources.reference(slot.request),resources,resolve));};
    prepare(slots_);prepare(retired_);
    for(auto& [slot,pixels]:ready){slot->current.image=std::move(pixels);slot->request.reset();}
}
}
