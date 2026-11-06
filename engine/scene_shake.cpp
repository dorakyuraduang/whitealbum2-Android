#include "scene_shake.hpp"
#include "checkpoint_codec.hpp"
#include <bit>
#include <cmath>
#include <limits>

namespace wa2::script {
namespace {
#include "weather_curve.inc"
constexpr std::array<int,8> horizontal{0,-1,-1,-1,0,1,1,1},vertical{1,1,0,-1,-1,-1,0,1};
}
SceneShake::SceneShake(std::function<std::uint32_t()> random):random_(std::move(random)){if(!random_)throw Error("Missing native shake random source");}
void SceneShake::validate(std::int32_t mode,std::int32_t amplitude,std::int32_t duration,std::int32_t direction,std::int32_t speed){
    const auto type=mode&255,scope=mode&~255;
    if((scope!=0 && scope!=0x100 && scope!=0x200) || (type!=0 && type!=1 && type!=2 && type!=9 && type!=12 && type!=13 && type!=15))
        throw Unsupported("Native shake mode "+std::to_string(mode));
    if(amplitude<0 || amplitude>46340 || duration< -2 || direction<0 || direction>7 || speed<0 || speed>65535 || ((type==2 || type==15) && amplitude==0))
        throw Unsupported("Native shake parameters out of range");
}
std::uint32_t SceneShake::random(){const auto value=random_();if(value>32767)throw Error("Native shake random value out of range");return value;}
void SceneShake::stop()noexcept{active_=false;elapsed_=0;world_={};dialogue_={};}
std::optional<CommandResult> SceneShake::dispatch(Command& call){
    if(call.index!=150 && call.index!=231)return {};
    if(call.arguments.size()!=(call.index==150?5u:6u))throw Error("Invalid native shake parameters");
    const auto v=[&](unsigned n){return call.arguments[n].integer();};
    if(call.index==231)v(5);
    if(!call.continuation){
        const auto mode=v(0),amplitude=v(1),duration=v(2),direction=v(3),speed=v(4);validate(mode,amplitude,duration,direction,speed);
        mode_=mode;amplitude_=amplitude;duration_=duration;direction_=direction;speed_=speed;elapsed_=0;active_=true;world_={};dialogue_={};call.continuation=1;
        // The cached setter retains the alternating random-direction phase
        // across new shakes; only its coordinates and transform are reset.
    }
    const bool complete=!active_ || duration_==0 || (call.index==231 && v(5)==0);
    return CommandResult{complete,!complete,Value{}};
}
void SceneShake::update(const std::function<std::int32_t(std::int32_t)>& adjust){
    if(!active_)return;
    const auto ticks=adjust?adjust(duration_):duration_==-1?15:duration_==-2?30:duration_;
    if(elapsed_==std::uint32_t(std::numeric_limits<std::int32_t>::max()))throw Error("Native continuous shake counter exhausted");
    ++elapsed_;if(duration_!=0 && std::int64_t(elapsed_)>=ticks){stop();return;}
    const auto type=mode_&255,scope=mode_&~255;image::SceneTransform pose;
    const auto wave=[&]{const auto phase=std::uint64_t(std::uint32_t(speed_))*elapsed_/8;
        auto amount=std::int64_t(weather_curve[phase%256])*amplitude_/4096;
        if(ticks!=0)amount=amount*(ticks-std::int64_t(elapsed_))/ticks;return amount;};
    if(type==0){const auto amount=wave();pose.x=float(amount*horizontal[direction_]);pose.y=float(amount*vertical[direction_]);}
    else if(type==1){const auto amount=std::int64_t((elapsed_%8)/4*2)-1;pose.x=float(amount*amplitude_*horizontal[direction_]);pose.y=float(amount*amplitude_*vertical[direction_]);}
    else if(type==9){
        if(!random_half_){auto direction=random()%8;unsigned retries{};
            while(direction==std::uint32_t(direction_)){if(++retries>65536)throw Error("Native shake random source failed to change direction");direction=random()%8;}
            direction_=int(direction);
        }random_half_=!random_half_;pose.x=float(amplitude_*horizontal[direction_]);pose.y=float(amplitude_*vertical[direction_]);
    }else if(type==2 || type==15){
        auto triangle=std::int64_t(elapsed_)*2%(std::int64_t(amplitude_)*2);if(triangle>=amplitude_)triangle=std::int64_t(amplitude_)*2-triangle;
        const auto gain=type==2?1.:double(speed_-100)/100.;
        pose.scale=float(1.+gain*double(triangle*triangle)/double(std::int64_t(amplitude_)*amplitude_));
    }else if(type==12){
        if(ticks){const auto remaining=360-std::int64_t(elapsed_)*360/ticks,phase=360-remaining*remaining/360;
            const auto angle=(phase*amplitude_/2)%360;pose.angle=float(direction_%2?angle:360-angle);
        }else{pose.angle=float(double(std::int64_t(elapsed_)*amplitude_)*(direction_%2?1.:-1.)/100.);pose.scale=float(double(speed_)/4096.);}
    }else if(type==13)pose.angle=float(-wave());
    if(scope==0x200){world_=pose;dialogue_=pose;}else if(scope==0x100)dialogue_=pose;else world_=pose;
}
std::vector<std::uint8_t> SceneShake::checkpoint()const{
    checkpoint::Writer out;for(auto value:{mode_,amplitude_,duration_,direction_,speed_})out.i32(value);out.u32(elapsed_);out.boolean(active_);out.boolean(random_half_);
    for(const auto* pose:{&world_,&dialogue_})for(const auto value:{pose->x,pose->y,pose->scale,pose->angle})out.u32(std::bit_cast<std::uint32_t>(value));
    return out.seal("LSSH");
}
void SceneShake::restore(std::span<const std::uint8_t> bytes){
    auto in=checkpoint::Reader::open("LSSH",bytes);const auto mode=in.i32(),amplitude=in.i32(),duration=in.i32(),direction=in.i32(),speed=in.i32();validate(mode,amplitude,duration,direction,speed);
    const auto elapsed=in.u32();const auto active=in.boolean(),half=in.boolean();image::SceneTransform world,dialogue;
    for(auto* pose:{&world,&dialogue}){pose->x=std::bit_cast<float>(in.u32());pose->y=std::bit_cast<float>(in.u32());pose->scale=std::bit_cast<float>(in.u32());pose->angle=std::bit_cast<float>(in.u32());
        for(auto value:{pose->x,pose->y,pose->scale,pose->angle})if(!std::isfinite(value) || std::abs(value)>1.e9f)throw Error("Invalid saved native shake transform");
        if(pose->scale<0 || pose->scale>1000)throw Error("Invalid saved native shake scale");}
    in.finish();if(elapsed>std::uint32_t(std::numeric_limits<std::int32_t>::max()) || (!active && (elapsed || !world.identity() || !dialogue.identity())))throw Error("Invalid saved native shake state");
    mode_=mode;amplitude_=amplitude;duration_=duration;direction_=direction;speed_=speed;elapsed_=elapsed;active_=active;random_half_=half;world_=world;dialogue_=dialogue;
}
}
