#include "scene_calendar.hpp"
#include "checkpoint_codec.hpp"
#include "resource_catalog.hpp"
#include <algorithm>
#include <bit>
#include <cmath>

namespace wa2::script {
namespace {
// Calendar facts in the retained 2010..2013 table; not renderer/script bytes.
constexpr std::array<std::array<unsigned,18>,4> holidays{{
    {101,111,211,321,322,429,503,504,505,719,920,923,1011,1103,1123,1223,0,0},
    {101,110,211,321,429,503,504,505,718,919,923,1010,1103,1123,1223,0,0,0},
    {101,102,109,211,320,429,430,503,504,505,716,917,922,1008,1103,1123,1223,1224},
    {101,114,211,320,429,503,504,505,506,715,916,923,1014,1103,1104,1123,1223,0}}};
auto alpha(std::int64_t value){return std::uint8_t(std::clamp(value,std::int64_t(0),std::int64_t(255)));}
bool display_year(std::int32_t year){
    // The installed 2029 script also requests 201-01-29. Cached source lookup
    // for that literal has no matching month/day pair. Preserve its year and
    // weekday with no holiday, without reproducing the source's off-table read.
    return year==201 || (year>=2010 && year<=2013);
}
}
CalendarDate SceneCalendar::date(std::int32_t year,std::int32_t month,std::int32_t day,std::int32_t weekday){
    if(year<1 || year>9999 || month<1 || month>12 || weekday< -1 || weekday>7)throw Error("Native calendar date out of range");
    // Source Calendar accepts month/day labels, including 2013-02-29 used by
    // the installed story. It computes a weekday without civil-date validation.
    if(day<1 || day>31)throw Error("Native calendar day is outside its label range");
    if(weekday==-1){const auto y=year-(month<=2),m=month+(month<=2?12:0);weekday=((13*m+8)/5+y/400-y/100+y/4+y+day)%7;}
    bool holiday=false;if(year>=2010 && year<=2013){const auto& row=holidays[std::size_t(year-2010)];holiday=std::find(row.begin(),row.end(),unsigned(month*100+day))!=row.end();}
    return {year,month,day,weekday,holiday};
}
bool SceneCalendar::supports_display(const CalendarDate& value)noexcept{return display_year(value.year) && value.weekday>=0 && value.weekday<=6;}
SceneCalendar::SceneCalendar(Loader loader,std::function<std::int32_t()> theme):loader_(std::move(loader)),theme_(std::move(theme)){
    if(!loader_)throw Error("Missing native calendar image loader");
    line_=std::make_shared<image::Raster>(image::Raster{1,1,{21,37,83,255}});sprites_.reserve(9);
}
std::array<std::shared_ptr<image::ImageRequest>,3> SceneCalendar::load(bool alternate)const{
    std::array<std::shared_ptr<image::ImageRequest>,3> requests{loader_(alternate?"calender_01.tga":"calender_00.tga"),loader_("calender_10.tga"),loader_("calender_11.tga")};
    for(const auto& request:requests)if(!request)throw Error("Native calendar loader returned no request");return requests;
}
bool SceneCalendar::ready(){
    bool ready=true;for(unsigned n=0;n<images_.size();++n)if(!images_[n]){images_[n]=requests_[n]->get();if(images_[n])requests_[n].reset();else ready=false;}
    if(ready && (images_[0]->width<1280 || images_[0]->height<720 || images_[1]->width<472 || images_[1]->height<48 || images_[2]->width<std::uint32_t((date_.weekday+1)*104) || images_[2]->height<32))
        throw Error("Native calendar atlas dimensions are too small");return ready;
}
void SceneCalendar::prepare_restored_assets(resource::Catalog& resources,const image::Resolve& resolve){
    if(!active_)return;for(unsigned n=0;n<requests_.size();++n)if(requests_[n]){images_[n]=image::resolve_reference(resources.reference(requests_[n]),resources,resolve);requests_[n].reset();}if(!ready())throw Error("Restored calendar assets remain incomplete");
}
std::optional<CommandResult> SceneCalendar::dispatch(Command& call){
    if(call.index!=203)return {};
    if(call.arguments.size()!=5)throw Error("Invalid native Calendar parameters");
    if(!call.continuation){
        const auto next=date(call.arguments[0].integer(),call.arguments[1].integer(),call.arguments[2].integer(),call.arguments[3].integer());
        const bool display=call.arguments[4].integer()!=0;
        if(display && !supports_display(next))throw Unsupported("Native calendar display date outside the recovered display cases");
        const bool alternate=display && theme_ && theme_()==2;
        auto requests=display?load(alternate):decltype(requests_){};
        date_=next;alternate_=alternate;requests_=std::move(requests);images_={};phase_=elapsed_=age_=bar_elapsed_=0;
        opacity_=0;digit_opacity_=255;bar_width_=bar_height_=bar_offset_=0;digits_=background_=bars_=false;active_=display;call.continuation=1;
    }
    return CommandResult{!active_,true,Value{}};
}
void SceneCalendar::update(const std::function<std::int32_t(std::int32_t)>& adjust){
    if(!active_ || !ready())return;
    const auto ticks=std::max(1,adjust?adjust(60):60),half=ticks/2;
    switch(phase_){
    case 0:phase_=1;digits_=true;[[fallthrough]];
    case 1:
        if(std::int64_t(elapsed_)>=half){opacity_=255;background_=true;phase_=2;elapsed_=bar_elapsed_=0;}
        else{background_=true;opacity_=alpha(std::uint64_t(elapsed_)*256/std::uint32_t(half));}break;
    case 2:
        bars_=true;
        if(std::int64_t(elapsed_)>=half){bar_height_=720;bar_width_=1280;phase_=3;elapsed_=0;}
        else bar_width_=bar_height_=float(std::uint64_t(bar_elapsed_)*5*256/std::uint32_t(half));break;
    case 3:if(std::int64_t(elapsed_)>=ticks){phase_=4;elapsed_=bar_elapsed_=0;}break;
    case 4:
        if(std::int64_t(elapsed_)>=ticks){phase_=5;digits_=background_=bars_=false;}
        else{bar_offset_=float(std::uint64_t(bar_elapsed_)*5*256/std::uint32_t(std::max(half,1)));
            opacity_=alpha(256-std::int64_t(std::uint64_t(elapsed_)*256/std::uint32_t(ticks)));
            digit_opacity_=alpha(256-std::int64_t(std::uint64_t(elapsed_)*512/std::uint32_t(ticks)));}break;
    case 5:active_=false;requests_={};images_={};break;
    default:throw Error("Invalid native calendar phase");
    }
    ++age_;++bar_elapsed_;++elapsed_;
}
std::span<const image::Sprite> SceneCalendar::sprites(){
    sprites_.clear();if(!active_ || !images_[0] || !images_[1] || !images_[2])return sprites_;
    const auto add=[&](std::shared_ptr<const image::Raster> image,float x,float y,float w,float h,float sx,float sy,float sw,float sh,std::uint8_t opacity,int layer=0x1062){
        if(!opacity)return;sprites_.push_back({std::move(image),x,y,w,h,sx,sy,sw,sh,255,255,255,opacity});sprites_.back().layer=layer;sprites_.back().order=0x53a+std::int32_t(sprites_.size());};
    if(background_)add(images_[0],0,0,1280,720,0,0,1280,720,opacity_,0x1061);
    if(bars_){add(line_,167,bar_offset_,1,bar_height_,0,0,1,1,128);add(line_,bar_offset_,235,bar_width_,1,0,0,1,1,128);}
    if(digits_){
        const float shift=float(age_?(age_-1)/2:0);
        const std::array<int,5> tile{date_.month>=10?date_.month/10:10,date_.month%10,11,date_.day>=10?date_.day/10:date_.day,date_.day>=10?date_.day%10:10};
        constexpr float x[]{276,316,356,388,428};for(unsigned n=0;n<tile.size();++n){const auto width=n==2?32.f:40.f;add(images_[1],x[n]-shift,176,width,48,float(tile[n]*40),0,width,48,digit_opacity_);}
        add(images_[2],476-shift,192,104,32,float(date_.weekday*104),0,104,32,digit_opacity_);
    }
    return sprites_;
}
std::vector<std::uint8_t> SceneCalendar::checkpoint()const{
    checkpoint::Writer out;out.i32(date_.year);out.i32(date_.month);out.i32(date_.day);out.i32(date_.weekday);
    for(auto v:{phase_,elapsed_,age_,bar_elapsed_,std::uint32_t(opacity_),std::uint32_t(digit_opacity_)})out.u32(v);
    out.u32(std::bit_cast<std::uint32_t>(bar_width_));out.u32(std::bit_cast<std::uint32_t>(bar_height_));out.u32(std::bit_cast<std::uint32_t>(bar_offset_));
    for(bool v:{active_,digits_,background_,bars_,alternate_})out.boolean(v);return out.seal("LSCL");
}
void SceneCalendar::restore(std::span<const std::uint8_t> bytes){
    auto in=checkpoint::Reader::open("LSCL",bytes);const auto year=in.i32(),month=in.i32(),day=in.i32(),weekday=in.i32();
    const auto next=year?date(year,month,day,weekday):CalendarDate{};
    const auto phase=in.u32(),elapsed=in.u32(),age=in.u32(),bars_elapsed=in.u32(),opacity=in.u32(),digit_opacity=in.u32();
    const auto width=std::bit_cast<float>(in.u32()),height=std::bit_cast<float>(in.u32()),offset=std::bit_cast<float>(in.u32());
    const auto active=in.boolean(),digits=in.boolean(),background=in.boolean(),bars=in.boolean(),alternate=in.boolean();in.finish();
    if(phase>5 || elapsed>1000000 || age>1000000 || bars_elapsed>1000000 || opacity>255 || digit_opacity>255 || !std::isfinite(width) || !std::isfinite(height) || !std::isfinite(offset) || width<0 || width>1.e6f || height<0 || height>1.e6f || offset<0 || offset>1.e6f || (!year && (month || day || weekday)) ||
       (active && !supports_display(next)))throw Error("Invalid saved native calendar state");
    auto requests=active?load(alternate):decltype(requests_){};
    date_=next;phase_=phase;elapsed_=elapsed;age_=age;bar_elapsed_=bars_elapsed;opacity_=std::uint8_t(opacity);digit_opacity_=std::uint8_t(digit_opacity);
    bar_width_=width;bar_height_=height;bar_offset_=offset;active_=active;digits_=digits;background_=background;bars_=bars;alternate_=alternate;requests_=std::move(requests);images_={};
}
}
