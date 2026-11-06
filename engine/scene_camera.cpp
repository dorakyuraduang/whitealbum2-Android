#include "scene_camera.hpp"
#include "checkpoint_codec.hpp"
#include <algorithm>
#include <bit>
#include <cmath>

namespace wa2::script {
namespace {
void coordinate(float x){if(!std::isfinite(x) || std::abs(x)>1.e9f)throw Error("Native camera coordinate out of range");}
void scale(float x){if(!std::isfinite(x) || x<=0 || x>10000)throw Error("Native camera scale out of range");}
}
float SceneCamera::ease(std::int32_t mode,float t){
    switch(mode){case 1:return t*t;case 2:return 1-(1-t)*(1-t);
    case 3:return t<.5f?2*t*t:.5f+.5f*(1-(2-2*t)*(2-2*t));
    case 4:return t<.5f?.5f*(1-(1-2*t)*(1-2*t)):.5f+2*(t-.5f)*(t-.5f);
    default:return t;}
}
void SceneCamera::start(Tween& tween,std::array<float,2> current,std::array<float,2> target,std::int32_t ticks,std::int32_t mode){
    tween={current,target,current,ticks,mode,0,true};
}
void SceneCamera::bind(unsigned bank,std::uint32_t width,std::uint32_t height,CameraPose pose){
    if(bank>=banks_.size() || !width || !height || width>65535 || height>65535)throw Error("Native camera image dimensions out of range");
    coordinate(pose.x);coordinate(pose.y);coordinate(pose.angle);coordinate(pose.pivot_x);coordinate(pose.pivot_y);scale(pose.scale_x);scale(pose.scale_y);
    banks_[bank]=Bank{};banks_[bank].pose=pose;banks_[bank].width=width;banks_[bank].height=height;
}
void SceneCamera::replace(unsigned index,std::uint32_t width,std::uint32_t height,CameraPose pose,CameraRetain retain){
    if(index>=banks_.size())throw Error("Native camera bank out of range");
    const auto own=banks_[index],previous=banks_[index^1];Tween pan,zoom=own.zoom,rotation=own.rotation;
    if(retain.x)pose.x=std::trunc(previous.pose.x);if(retain.y)pose.y=std::trunc(previous.pose.y);
    if(retain.scale_x)pose.scale_x=previous.pose.scale_x;if(retain.scale_y)pose.scale_y=previous.pose.scale_y;
    pose.pivot_x=retain.scale_x?own.pose.pivot_x:640;pose.pivot_y=retain.scale_y?own.pose.pivot_y:360;
    if(retain.angle)pose.angle=previous.rotation.active?previous.rotation.from[0]:previous.pose.angle;
    const auto clock=[](Tween& to,const Tween& from){to.duration=from.duration;to.easing=from.easing;to.elapsed=from.elapsed;to.active=from.active;};
    const std::array<bool,2> keep_pan{retain.x,retain.y},keep_zoom{retain.scale_x,retain.scale_y};
    const std::array<float,2> position{pose.x,pose.y},scale_values{pose.scale_x,pose.scale_y};
    for(unsigned axis=0;axis<2;++axis){
        pan.from[axis]=pan.target[axis]=pan.current[axis]=position[axis];
        if(keep_pan[axis] && previous.pan.active){pan.from[axis]=previous.pan.from[axis];pan.target[axis]=previous.pan.target[axis];clock(pan,previous.pan);}
        else if(!keep_pan[axis])pan.active=false;
        zoom.from[axis]=zoom.target[axis]=zoom.current[axis]=scale_values[axis];
        if(keep_zoom[axis] && previous.zoom.active){zoom.from[axis]=previous.zoom.from[axis];zoom.target[axis]=previous.zoom.target[axis];clock(zoom,previous.zoom);}
        else if(!keep_zoom[axis])zoom.active=false;
    }
    // Cached setters share one activity flag for each pair of axes. Their
    // X-then-Y order matters when only one component uses the retain sentinel.
    // Retained scale/angle axes keep the receiving bank's pivot/old wait clock
    // when there is no active donor; all corresponding endpoints are replaced.
    rotation.from=rotation.target=rotation.current={pose.angle,0};if(!retain.angle)rotation.active=false;
    bind(index,width,height,pose);banks_[index].pan=pan;banks_[index].zoom=zoom;banks_[index].rotation=rotation;
}
const CameraPose& SceneCamera::pose(unsigned bank)const{if(bank>=banks_.size())throw Error("Native camera bank out of range");return banks_[bank].pose;}
bool SceneCamera::busy()const noexcept{for(const auto& bank:banks_)if(bank.pan.active || bank.zoom.active || bank.rotation.active)return true;return false;}
void SceneCamera::update(const std::function<std::int32_t(std::int32_t)>& duration){
    if(paused_){++pause_elapsed_;if(std::int64_t(pause_elapsed_)>=pause_ticks_){paused_=0;pause_ticks_=0;pause_elapsed_=0;}}
    for(auto& bank:banks_){
        const auto advance=[&](Tween& tween){
            if(!tween.active)return;
            if(!paused_)++tween.elapsed;
            const auto ticks=duration?duration(tween.duration):tween.duration==-1?15:tween.duration==-2?30:tween.duration;
            if(ticks<=0 || std::int64_t(tween.elapsed)>=ticks){tween.current=tween.target;tween.active=false;}
            else{const auto t=ease(tween.easing,float(tween.elapsed)/float(ticks));for(unsigned n=0;n<2;++n)tween.current[n]=tween.from[n]*(1-t)+tween.target[n]*t;}
        };
        const auto pan=bank.pan.active,zoom=bank.zoom.active,rotation=bank.rotation.active;
        advance(bank.pan);advance(bank.zoom);advance(bank.rotation);
        if(pan){bank.pose.x=bank.pan.current[0];bank.pose.y=bank.pan.current[1];}
        if(zoom){bank.pose.scale_x=bank.zoom.current[0];bank.pose.scale_y=bank.zoom.current[1];}
        if(rotation)bank.pose.angle=bank.rotation.current[0];
    }
}
std::optional<CommandResult> SceneCamera::dispatch(Command& command,unsigned index){
    const auto id=command.index;if(id<210 || id>214)return {};
    if(index>=banks_.size())throw Error("Native camera bank out of range");auto& bank=banks_[index];
    const auto count=id==210?5u:id==211?7u:id==212?6u:id==213?0u:2u;
    if(command.arguments.size()!=count)throw Error("Invalid native camera command parameters");
    const auto integer=[&](unsigned n){return command.arguments[n].integer();};
    if(id==213)return CommandResult{!busy(),true,Value{}};
    if(id==214){paused_=integer(0);pause_ticks_=integer(1);pause_elapsed_=0;return CommandResult{true,false,Value{}};}
    if(id==210){
        const auto x=float((1280-std::int64_t(bank.width))/2+std::int64_t(integer(0))),y=float((720-std::int64_t(bank.height))/2+std::int64_t(integer(1)));coordinate(x);coordinate(y);
        start(bank.pan,{std::trunc(bank.pose.x),std::trunc(bank.pose.y)},{x,y},integer(2),integer(3));
    }else if(id==211){
        const auto x=command.arguments[0].real(),y=command.arguments[1].real();scale(x);scale(y);
        start(bank.zoom,{bank.pose.scale_x,bank.pose.scale_y},{x,y},integer(2),integer(3));bank.pose.pivot_x=float(integer(4));bank.pose.pivot_y=float(integer(5));
    }else{
        const auto angle=command.arguments[0].real();coordinate(angle);start(bank.rotation,{bank.pose.angle,0},{angle,0},integer(1),integer(2));bank.pose.pivot_x=float(integer(3));bank.pose.pivot_y=float(integer(4));
    }
    return CommandResult{true,false,Value{}};
}
std::vector<std::uint8_t> SceneCamera::checkpoint()const{
    checkpoint::Writer out;out.i32(paused_);out.i32(pause_ticks_);out.u32(pause_elapsed_);
    const auto real=[&](float v){out.u32(std::bit_cast<std::uint32_t>(v));};
    for(const auto& b:banks_){out.u32(b.width);out.u32(b.height);for(auto v:{b.pose.x,b.pose.y,b.pose.scale_x,b.pose.scale_y,b.pose.angle,b.pose.pivot_x,b.pose.pivot_y})real(v);
        for(const auto* t:{&b.pan,&b.zoom,&b.rotation}){for(auto values:{t->from,t->target,t->current})for(auto v:values)real(v);out.i32(t->duration);out.i32(t->easing);out.u32(t->elapsed);out.boolean(t->active);}}
    return out.seal("LSCA");
}
void SceneCamera::restore(std::span<const std::uint8_t> bytes){
    auto in=checkpoint::Reader::open("LSCA",bytes);const auto paused=in.i32(),ticks=in.i32();const auto elapsed=in.u32();std::array<Bank,2> next;
    const auto real=[&]{const auto v=std::bit_cast<float>(in.u32());coordinate(v);return v;};
    for(auto& b:next){b.width=in.u32();b.height=in.u32();if(!b.width || !b.height || b.width>65535 || b.height>65535)throw Error("Invalid saved camera image dimensions");
        b.pose={real(),real(),real(),real(),real(),real(),real()};scale(b.pose.scale_x);scale(b.pose.scale_y);
        for(auto* t:{&b.pan,&b.zoom,&b.rotation}){for(auto* values:{&t->from,&t->target,&t->current})for(auto& v:*values)v=real();t->duration=in.i32();t->easing=in.i32();t->elapsed=in.u32();t->active=in.boolean();}
        if(b.zoom.active)for(auto values:{b.zoom.from,b.zoom.target,b.zoom.current})for(auto v:values)scale(v);
    }
    in.finish();banks_=std::move(next);paused_=paused;pause_ticks_=ticks;pause_elapsed_=elapsed;
}
}
