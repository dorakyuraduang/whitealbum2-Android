#include "scene_weather.hpp"
#include <algorithm>
#include <bit>
#include <cmath>

namespace wa2::script {
namespace {
#include "weather_curve.inc"
int integer(double number){
    if(!std::isfinite(number) || number<-2147483648.0 || number>=2147483648.0)return -2147483647-1;
    return int(number);
}
std::int32_t density(std::int32_t value){return std::clamp(value,0,400);}
void approach(float& value,std::int32_t target){
    if(value<target)value=std::min(value+1,float(target));
    if(value>target)value=std::max(value-1,float(target));
}
}
SceneWeather::SceneWeather(Loader loader,Random random):loader_(std::move(loader)),random_(std::move(random)){
    if(!loader_ || !random_)throw Error("missing native weather loader or random source");sprites_.reserve(400);
}
std::uint32_t SceneWeather::random(){const auto value=random_();if(value>32767)throw Error("native game random value out of range");return value;}
void SceneWeather::create(std::size_t index,bool filled,bool opposite){
    auto& p=particles_.at(index);p={};p.active=true;
    if(type_==3){
        // Existing 0x407190, including RNG calls whose first speed values are
        // overwritten: later particles must consume the same random sequence.
        const auto category=random()%32;
        if(category==0){
            p.kind=std::int16_t(random()%16);
            random();random();p.rotation=float(random()%8+10);p.scale=float(random()%8+10);
        }else if(category<3){
            p.kind=std::int16_t(random()%48+192);p.rotation=float(random()%4+5);p.scale=float(random()%4+5);
        }else{
            p.kind=std::int16_t(random()%8+248);
            p.rotation=float((double(random()%((256-p.kind)*100))/100.0+1.0)*.5);
            p.scale=float((double(random()%((256-p.kind)*100))/100.0+1.0)*.5);
        }
        p.angle=float(int(random()%((576-p.kind)*4))-(256-p.kind)*2);
        p.radius=float(filled?int(random()%((436-p.kind)*4))-(256-p.kind)*2:-int(random()%100)-(256-p.kind)*2);
        p.rate=float(p.kind);p.phase=std::int16_t(random()%256);return;
    }
    p.kind=std::int16_t(random()%4);
    p.angle=float(double(random()%360)/3.141592);
    p.radius=float(filled?random()%1300+100:random()%100+(opposite?1300:100));
    if(filled){p.phase=std::int16_t(random()%360);p.scale=float(random()%360);}
    else{p.phase=0;p.scale=1;}
    p.rate=float(double(random()%30)/10.0-1.0);
    if(p.rate>=0 && p.rate<.4f)p.rate=.4f;
    if(p.rate<0 && p.rate>-.4f)p.rate=-.4f;
    p.rotation=float(random()%360);
    p.scale=float((double(random()%((4-p.kind)*100))/100.0+1.0)*.5);
}
void SceneWeather::begin(Command& command){
    if(command.arguments.size()!=6)throw Error("invalid native SetWeather parameters");
    const auto code=command.arguments[0].integer(),mode=code&255;
    if((mode!=3 && mode!=4) || (code&~0xfff)!=0)throw Unsupported("unimplemented native weather type "+std::to_string(code));
    if((code&0xe00)!=0)throw Unsupported("unimplemented native weather colour flags");
    const auto priority=command.arguments[4].integer(),scope=command.arguments[5].integer();
    if(priority<0 || priority>2 || scope<0 || scope>2)throw Unsupported("unimplemented native weather layer");
    auto request=loader_("weather.tga");if(!request)throw Error("native weather loader returned no request");
    stop(true);particles_={};extent_=0;frame_=0;stopping_=false;
    type_=mode;prefill_=(code&0x100)!=0;priority_=priority;scope_=scope;
    target_x_=command.arguments[1].integer();target_y_=command.arguments[2].integer();
    density_=density(command.arguments[3].integer());speed_x_=float(target_x_);speed_y_=float(target_y_);
    amplitude_=target_amplitude_=0;request_=std::move(request);image_.reset();enabled_=true;
}
void SceneWeather::stop(bool immediately){
    if(!immediately){density_=0;stopping_=true;return;}
    if(scope_==2){scope_=0;return;}
    particles_={};sprites_.clear();request_.reset();image_.reset();extent_=0;frame_=0;enabled_=false;stopping_=false;
}
bool SceneWeather::stopped(){
    if(!stopping_)return false;
    for(std::size_t n=0;n<extent_;++n)if(particles_[n].active)return false;
    stop(true);return true;
}
std::optional<CommandResult> SceneWeather::dispatch(Command& command){
    const auto count=[&](std::size_t n){if(command.arguments.size()!=n)throw Error("invalid native weather parameters");};
    const auto value=[&](std::size_t n){return command.arguments[n].integer();};
    switch(command.index){
    case 173:
        if(!command.continuation){begin(command);command.continuation=1;}
        return CommandResult{!loading(),false,Value{}};
    case 174:
        count(5);
        if(value(3)!=-1000 && (value(3)<0 || value(3)>2))throw Unsupported("unimplemented native weather priority");
        if(value(4)!=-1000 && (value(4)<0 || value(4)>2))throw Unsupported("unimplemented native weather scope");
        if(value(0)!=-1000)target_x_=value(0);if(value(1)!=-1000)target_y_=value(1);
        target_amplitude_=0;if(value(2)!=-1000)density_=density(value(2));
        if(value(3)!=-1000)priority_=value(3);if(value(4)!=-1000)scope_=value(4);
        return CommandResult{true,false,Value{}};
    case 175:
        count(0);if(!command.continuation){stop(false);command.continuation=1;}
        return CommandResult{stopped(),true,Value{}};
    default:return {};
    }
}
void SceneWeather::update(){
    if(!enabled_)return;
    if(request_){auto image=request_->get();if(!image)return;
        if(image->width<32 || image->height<64)throw Error("native weather atlas is too small");
        image_=std::move(image);request_.reset();}
    approach(speed_x_,target_x_);approach(speed_y_,target_y_);
    if(amplitude_<target_amplitude_)++amplitude_;
    ++frame_;
    if(prefill_){prefill_=false;for(std::size_t n=0;n<std::size_t(density_*4/5);++n){create(n,true);++extent_;}}
    const auto root=float(std::sqrt(double(density_)));
    const auto period=std::max(4,integer(100.0/std::max(1.f,root)));
    if(extent_<std::size_t(density_) && frame_%std::uint32_t(period)==0){create(extent_,false);++extent_;}
    sprites_.clear();
    for(std::size_t n=0;n<extent_;++n){auto& p=particles_[n];if(!p.active)continue;
        if(type_==3){
            const auto wave=weather_curve[(unsigned(p.phase)+64)&255];
            const auto sway=float((double(speed_y_)*wave*p.rotation*(1.0/4096.0))/40.0);
            const auto drift=float(double(p.rotation)*speed_x_/10.0);
            p.angle=float(double(p.angle)+drift+sway);p.radius=float(double(p.scale)*speed_y_/10.0+p.radius);
            const auto retire=[&]{if(n<std::size_t(density_))return false;--extent_;p.active=false;return true;};
            if(p.radius>720){if(retire())continue;create(n,false);}
            const auto boundary=float((896-p.kind)*2);
            if(p.angle>boundary){if(retire())continue;p.angle-=boundary;}
            if(p.angle<float(p.kind*4-1024)){if(retire())continue;p.angle+=boundary;}
            const auto alpha=p.rate<=16?32:p.rate<240?128:integer((256.0-p.rate)*8+32);
            const auto size=float((257.0-p.rate)*(p.kind<241?2:4));
            sprites_.push_back({image_,float(integer(p.angle)),float(integer(p.radius)),size,size,0,32,32,32,
                255,255,255,std::uint8_t(std::clamp(alpha,0,255))});
            sprites_.back().layer=scope_?0x1043:p.kind<241?10:1;sprites_.back().order=0x99+std::int32_t(n);
            p.phase=std::bit_cast<std::int16_t>(std::uint16_t(std::uint16_t(p.phase)+1));continue;
        }
        p.rotation=p.rotation>360?0:float(double(p.rotation)+.2);
        p.radius=float((double(p.radius)+100.0+speed_y_)/400.0+p.radius);
        if(p.radius>=1400){
            if(n>=std::size_t(density_)){--extent_;p.active=false;continue;}
            create(n,false,true);p.radius=float(random()%100+100);
        }
        const auto angle=std::sin(double(p.rotation)/3.141592)/(double(p.radius)*.5+1.0)+p.angle;
        const auto base_x=integer(std::cos(angle)*p.radius*p.rate+640.0),base_y=integer(std::sin(angle)*p.radius*p.rate+360.0);
        const auto wave=[&](int at){return double(weather_curve[unsigned(at)&255])*p.phase*(1.0/4096.0)*amplitude_/100.0;};
        const auto x=integer(base_x+wave(p.phase)),y=integer(base_y+wave(p.phase+64));
        double alpha;
        if(p.radius<=450)alpha=(double(p.radius)-200)*.5;
        else if(p.radius>=1300)alpha=32-(double(p.radius)-1300)*32/100;
        else alpha=std::clamp(125-(double(p.radius)-450)*.25,32.0,255.0);
        const auto size=float(((double(5-p.kind)*p.radius)/100.0)/std::abs(p.rate)*.5);
        if(!std::isfinite(size))throw Error("native weather particle has invalid size");
        sprites_.push_back({image_,float(x),float(y),size,size,0,32,32,32,255,255,255,std::uint8_t(std::clamp(integer(alpha),0,255))});
        sprites_.back().layer=scope_?(priority_==0?0x1034:priority_==1?0x1039:0x1043):10;sprites_.back().order=0x99+std::int32_t(n);
        p.phase=std::bit_cast<std::int16_t>(std::uint16_t(std::uint16_t(p.phase)+1));
    }
}
}
