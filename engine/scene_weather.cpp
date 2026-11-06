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
SceneWeather::SceneWeather(Loader loader,Random random,AnimationLoader animations):loader_(std::move(loader)),random_(std::move(random)),animation_loader_(std::move(animations)){
    if(!loader_ || !random_)throw Error("missing native weather loader or random source");sprites_.reserve(400);
}
std::uint32_t SceneWeather::random(){const auto value=random_();if(value>32767)throw Error("native game random value out of range");return value;}
void SceneWeather::create(std::size_t index,bool filled,bool opposite){
    auto& p=particles_.at(index);p={};p.active=true;
    if(type_==1){
        const auto kind=[&]{p.kind=random()%20==0?0:std::int16_t(random()%2+1);};
        // Prefilled and newly spawned petals consume RNG in different orders.
        if(filled){kind();p.angle=float(random()%1280);p.radius=float(random()%720);}
        else{p.angle=float(random()%1280);p.radius=float(-40-int(random()%100));kind();}
        if(!p.kind)p.rotation=p.scale=8;
        else{const auto span=(3-p.kind)*100;p.rotation=float((double(random()%span)/100.0+1.0)*.5);p.scale=float((double(random()%span)/100.0+1.0)*.5);}
        p.phase=std::int16_t(random()%256);return;
    }
    if(type_==3){
        // Existing 0x407190, including RNG calls whose first speed values are
        // overwritten: later particles must consume the same random sequence.
        const auto category=random()%32;
        bool medium=category>0 && category<3,small=category>=3;
        if(category==0){
            p.kind=std::int16_t(random()%16);
            random();random();p.rotation=float(random()%8+10);p.scale=float(random()%8+10);
            medium=(flags_&0x200)!=0;
        }
        if(medium){
            p.kind=std::int16_t(random()%48+192);p.rotation=float(random()%4+5);p.scale=float(random()%4+5);
            small=(flags_&0x400)!=0;
        }
        if(small){
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
    if(type_==6 && filled){p.growth=1;p.velocity=0;
        for(int n=0;n<p.phase;++n){p.velocity=float(double(p.velocity)+(double(p.kind+2)/1000.0)*speed_y_/10.0);p.growth+=p.velocity;}}
    p.rate=float(double(random()%30)/10.0-1.0);
    if(p.rate>=0 && p.rate<.4f)p.rate=.4f;
    if(p.rate<0 && p.rate>-.4f)p.rate=-.4f;
    p.rotation=float(random()%360);
    p.scale=float((double(random()%((4-p.kind)*100))/100.0+1.0)*.5);
}
void SceneWeather::begin(Command& command){
    const bool extended=command.index==229;
    if(command.arguments.size()!=(extended?7u:6u))throw Error("invalid native SetWeather parameters");
    const auto code=command.arguments[0].integer(),mode=code&255;
    const auto amplitude=extended?command.arguments[6].integer():0;
    if((mode!=0 && mode!=1 && mode!=3 && mode!=4 && mode!=6) || (code&~0xfff)!=0)throw Unsupported("unimplemented native weather type "+std::to_string(code));
    if((code&0x800)!=0)throw Unsupported("unimplemented native weather alternate-atlas flag");
    const auto priority=command.arguments[4].integer(),scope=command.arguments[5].integer();
    if(priority<0 || priority>2 || scope<0 || scope>2)throw Unsupported("unimplemented native weather layer");
    if(mode==0){
        if(!animation_loader_)throw Unsupported("Missing native rain animation loader");
        auto definition=animation_loader_("\x89\x4a" "640x360.ani");if(!definition)throw Error("Native rain loader returned no animation");
        if(definition->textures().size()>8)throw Unsupported("Native rain has more than eight concurrent textures");
        std::vector<std::shared_ptr<image::ImageRequest>> requests;
        for(const auto& name:definition->textures()){auto request=loader_(name);if(!request)throw Error("Native rain loader returned no texture request");requests.push_back(std::move(request));}
        stop(true);particles_={};sprites_.clear();extent_=0;frame_=0;stopping_=false;type_=0;priority_=priority;scope_=scope;
        request_.reset();image_.reset();animation_=std::move(definition);animation_player_.emplace(animation_);animation_requests_=std::move(requests);
        sakura_requests_={};sakura_images_={};
        animation_textures_.assign(animation_requests_.size(),{});animation_validated_=false;enabled_=true;return;
    }
    std::array<std::shared_ptr<image::ImageRequest>,3> sakura;
    std::shared_ptr<image::ImageRequest> request;
    if(mode==1){constexpr std::string_view names[]{"sakura1_2.tga","sakura2.tga","sakura3.tga"};for(unsigned n=0;n<sakura.size();++n){sakura[n]=loader_(names[n]);if(!sakura[n])throw Error("Native sakura loader returned no texture request");}}
    else{request=loader_("weather.tga");if(!request)throw Error("native weather loader returned no request");}
    stop(true);particles_={};extent_=0;frame_=0;stopping_=false;
    // Replacing even an independent rain effect releases its old animation
    // definition/requests. Independence preserves background transitions only.
    animation_player_.reset();animation_.reset();animation_requests_.clear();animation_textures_.clear();animation_validated_=false;
    sakura_requests_=std::move(sakura);sakura_images_={};
    type_=mode;flags_=code&0xe00;prefill_=(code&0x100)!=0;priority_=priority;scope_=scope;
    target_x_=command.arguments[1].integer();target_y_=command.arguments[2].integer();
    density_=density(command.arguments[3].integer());speed_x_=float(target_x_);speed_y_=float(target_y_);
    amplitude_=amplitude;target_amplitude_=0;request_=std::move(request);image_.reset();enabled_=true;
}
void SceneWeather::stop(bool immediately){
    if(!immediately){density_=0;stopping_=true;return;}
    if(scope_==2){scope_=0;return;}
    particles_={};sprites_.clear();request_.reset();image_.reset();animation_player_.reset();animation_.reset();animation_requests_.clear();animation_textures_.clear();sakura_requests_={};sakura_images_={};animation_validated_=false;extent_=0;frame_=0;enabled_=false;stopping_=false;
}
bool SceneWeather::stopped(){
    if(!stopping_)return false;
    for(const auto& particle:particles_)if(particle.active)return false;
    stop(true);return true;
}
std::optional<CommandResult> SceneWeather::dispatch(Command& command){
    const auto count=[&](std::size_t n){if(command.arguments.size()!=n)throw Error("invalid native weather parameters");};
    const auto value=[&](std::size_t n){return command.arguments[n].integer();};
    switch(command.index){
    case 173:case 229:
        if(!command.continuation){begin(command);command.continuation=1;}
        return CommandResult{!loading(),false,Value{}};
    case 174:case 230:
        count(command.index==174?5:6);
        if(value(3)!=-1000 && (value(3)<0 || value(3)>2))throw Unsupported("unimplemented native weather priority");
        if(value(4)!=-1000 && (value(4)<0 || value(4)>2))throw Unsupported("unimplemented native weather scope");
        if(value(0)!=-1000)target_x_=value(0);if(value(1)!=-1000)target_y_=value(1);
        if(command.index==174)target_amplitude_=0;else if(value(5)!=-1000)target_amplitude_=value(5);
        if(value(2)!=-1000)density_=density(value(2));
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
    if(type_==0){
        for(std::size_t n=0;n<animation_requests_.size();++n)if(animation_requests_[n])if(auto image=animation_requests_[n]->get()){animation_textures_[n]=std::move(image);animation_requests_[n].reset();}
        if(loading())return;
        if(!animation_player_ || !animation_)throw Error("Missing native rain playback state");
        if(!animation_validated_){for(const auto& frame:animation_->frames())for(const auto& piece:frame.pieces){const auto& texture=animation_textures_.at(piece.texture);
                if(unsigned(piece.source_x)+unsigned(piece.width)>texture->width || unsigned(piece.source_y)+unsigned(piece.height)>texture->height)throw Error("Rain frame crop exceeds its texture");}animation_validated_=true;}
        animation_player_->tick();sprites_.clear();if(auto selected=animation_player_->frame())for(const auto& piece:animation_->frames()[*selected].pieces){if(piece.empty())continue;
            // Cached SetWeather's rain instance: centre/pivot (640,360), scale
            // 2, foreground priority 0x1043. INAS crop offsets include trimming.
            sprites_.push_back({animation_textures_.at(piece.texture),640.f+piece.x*2.f,360.f+piece.y*2.f,piece.width*2.f,piece.height*2.f,
                float(piece.source_x),float(piece.source_y),float(piece.width),float(piece.height)});
            sprites_.back().layer=0x1043;sprites_.back().order=0x51;
        }return;
    }
    if(type_==1){
        for(unsigned n=0;n<sakura_requests_.size();++n)if(sakura_requests_[n])if(auto image=sakura_requests_[n]->get()){sakura_images_[n]=std::move(image);sakura_requests_[n].reset();}
        if(loading())return;
        if(!animation_validated_){constexpr unsigned cell[]{128,32,16};for(unsigned n=0;n<3;++n)if(!sakura_images_[n] || sakura_images_[n]->width<12*cell[n] || sakura_images_[n]->height<2*cell[n])throw Error("Native sakura atlas dimensions are too small");animation_validated_=true;}
    }
    if(request_){auto image=request_->get();if(!image)return;
        if(image->width<32 || image->height<64)throw Error("native weather atlas is too small");
        image_=std::move(image);request_.reset();}
    approach(speed_x_,target_x_);approach(speed_y_,target_y_);
    if(amplitude_<target_amplitude_)++amplitude_;
    ++frame_;
    if(prefill_){prefill_=false;for(std::size_t n=0;n<std::size_t(density_*4/5);++n){create(n,true);++extent_;}}
    const auto root=float(std::sqrt(double(density_)));
    const auto period=std::max(4,integer(100.0/std::max(1.f,root)));
    if(extent_<std::size_t(density_) && frame_%std::uint32_t(period)==0){
        for(std::size_t n=0;n<std::size_t(density_);++n)if(!particles_[n].active){create(n,false);++extent_;break;}}
    sprites_.clear();
    // Active count and slot index are different after density decreases. Walk
    // fixed live slots so a retiring low slot cannot strand a higher particle.
    for(std::size_t n=0;n<particles_.size();++n){auto& p=particles_[n];if(!p.active)continue;
        if(type_==1){
            p.angle=float(double(p.angle)+double(weather_curve[(unsigned(p.phase)+64)&255])*p.rotation*(1.0/4096.0)+double(p.rotation)*speed_x_/10.0);
            p.radius=float(double(p.radius)+double(p.scale)*speed_y_/10.0);
            const auto retire=[&]{if(n<std::size_t(density_))return false;p.active=false;--extent_;return true;};
            if(p.radius>=720){if(retire())continue;p.radius-=752;}
            if(p.angle>=1280){if(retire())continue;p.angle-=1312;}
            if(p.angle< -32){if(retire())continue;p.angle+=1312;}
            constexpr unsigned cell[]{128,32,16},divisor[]{4,3,2},period[]{768,2304,768};const auto kind=unsigned(p.kind),frame=unsigned(p.phase)/divisor[kind]%24;
            const auto size=float(cell[kind]);sprites_.push_back({sakura_images_[kind],float(integer(p.angle)),float(integer(p.radius)),size,size,float(frame%12*cell[kind]),float(frame/12*cell[kind]),size,size});
            sprites_.back().layer=scope_?0x1043:kind==2?1:10;sprites_.back().order=0x99+std::int32_t(n);
            // The curve and flipbook share this period. Keeping their joint
            // phase bounded preserves every sample without signed overflow.
            p.phase=std::int16_t((unsigned(p.phase)+1)%period[kind]);continue;
        }
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
        if(type_==6){
            p.rotation=p.rotation>360?0:float(double(p.rotation)+(double(p.kind+2)/10.0)*speed_x_/10.0);
            const auto unwrapped=float(double(p.rotation)+(double(p.scale)*p.phase/5.0)*speed_x_/10.0);
            const auto angle=double(float(double(unwrapped)-std::trunc(double(unwrapped)/360.0)*360.0))*.01745329238474369;
            const auto base_x=integer(640.0-std::cos(angle)*p.growth*speed_y_*.5),base_y=integer(360.0+std::sin(angle)*p.growth*speed_y_*.5);
            const auto wave=[&](int at){return double(weather_curve[unsigned(at)&255])*p.phase*(1.0/4096.0)*amplitude_/100.0;};
            const auto x=integer(base_x+wave(p.phase)),y=integer(base_y+wave(p.phase+64));
            const auto boundary=-300.0-double(amplitude_)*2;
            if(x<boundary || x>=1580.0+double(amplitude_)*2 || y<boundary || y>=1020.0+double(amplitude_)*2){p.active=false;--extent_;continue;}
            p.velocity=float(double(p.velocity)+(double(p.kind+2)/1000.0)*speed_y_/20.0);p.growth+=p.velocity;
            const auto spread=double(p.growth)*speed_y_;
            const auto alpha=integer(std::clamp(spread<=450?spread-30:200-(spread-450)/5,0.0,200.0));
            const auto size=float((5-p.kind)*std::min(1.f,p.growth)*.25);
            if(!std::isfinite(size) || size<0)throw Unsupported("Native type-6 weather inverted particle growth");
            sprites_.push_back({image_,float(x),float(y),size,size,0,0,32,32,255,255,255,std::uint8_t(alpha)});
            sprites_.back().layer=scope_?(priority_==0?0x1034:priority_==1?0x1039:0x1043):10;sprites_.back().order=0x99+std::int32_t(n);
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
