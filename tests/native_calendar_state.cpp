#include "scene_calendar.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <thread>

namespace s=wa2::script;namespace r=wa2::resource;namespace i=wa2::image;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
template<class F>void rejects(F&& f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}CHECK(caught);}
struct Bytes:r::Source {std::vector<std::uint8_t> bytes;std::uint64_t size()const override{return bytes.size();}
    void read_at(std::uint64_t at,std::span<std::uint8_t> out)override{if(at>bytes.size() || out.size()>bytes.size()-at)throw r::Error("Calendar fixture bounds");if(!out.empty())std::memcpy(out.data(),bytes.data()+std::size_t(at),out.size());}};
void put(std::vector<std::uint8_t>& b,std::size_t at,std::uint32_t v){for(unsigned n=0;n<4;++n)b.at(at+n)=std::uint8_t(v>>(8*n));}
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{},guards{};
    CHECK(s::SceneCalendar::date(2010,11,24)==(s::CalendarDate{2010,11,24,3,false}));++cases;
    CHECK(s::SceneCalendar::date(2011,1,10).weekday==1 && s::SceneCalendar::date(2011,1,10).holiday);++cases;
    CHECK(s::SceneCalendar::date(2012,2,29).weekday==3 && s::SceneCalendar::date(2013,11,4).holiday);++cases;
    CHECK(s::SceneCalendar::date(2010,1,1,7).weekday==7 && s::SceneCalendar::date(2000,2,29).weekday==2);++cases;
    CHECK(s::SceneCalendar::date(1900,2,29).day==29 && s::SceneCalendar::date(2010,4,31).day==31);++cases;
    rejects([&]{s::SceneCalendar::date(1900,2,32);});rejects([&]{s::SceneCalendar::date(2010,4,0);});rejects([&]{s::SceneCalendar::date(0,1,1);});guards+=3;
    auto source=std::make_shared<Bytes>();auto& b=source->bytes;b.resize(60+18+1280*720*3);std::memcpy(b.data(),"KCAP",4);put(b,12,1);
    std::memcpy(b.data()+20,"calendar-test.tga",17);put(b,52,60);put(b,56,std::uint32_t(b.size()-60));b[62]=2;b[72]=0;b[73]=5;b[74]=208;b[75]=2;b[76]=24;b[77]=32;
    i::ImageStore images(r::Archive::open(source));auto request=images.request("calendar-test.tga");std::shared_ptr<const i::Raster> image;const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(!(image=request->get()) && std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(std::chrono::milliseconds(1));CHECK(image);
    std::vector<std::string> names;const auto loader=[&](auto name){names.emplace_back(name);return request;};s::SceneCalendar calendar(loader,[]{return 2;});
    const auto idle=calendar.checkpoint();calendar.restore(idle);CHECK(calendar.checkpoint()==idle && names.empty());++cases;
    std::array<s::Value,5> args{s::Value(201),s::Value(3),s::Value(3),s::Value(7),s::Value(0)};s::Command call{203,args};CHECK(calendar.dispatch(call)->complete && names.empty() && calendar.date().year==201);++cases;
    args={s::Value(2010),s::Value(11),s::Value(24),s::Value(-1),s::Value(1)};call={203,args};CHECK(!calendar.dispatch(call)->complete);
    CHECK(names==std::vector<std::string>({"calender_01.tga","calender_10.tga","calender_11.tga"}));calendar.update();auto sprites=calendar.sprites();
    CHECK(sprites.size()==6 && sprites[0].x==276 && sprites[0].y==176 && sprites[0].source_x==40 && sprites[2].source_x==440 && sprites[5].source_x==312);++cases;
    unsigned ticks=1;for(;ticks<40;++ticks)calendar.update();const auto saved=calendar.checkpoint();s::SceneCalendar restored(loader);restored.restore(saved);CHECK(restored.checkpoint()==saved);++cases;
    bool full_bars=false;for(;ticks<182;++ticks){calendar.update();restored.update();CHECK(calendar.checkpoint()==restored.checkpoint());
        for(const auto& sprite:calendar.sprites())if(sprite.width==1280 && sprite.height==1)full_bars=true;}
    CHECK(!calendar.busy() && calendar.dispatch(call)->complete && calendar.sprites().empty() && full_bars);++cases;
    call={203,args};calendar.dispatch(call);unsigned fast{};while(calendar.busy() && fast<200){calendar.update([](auto frames){return frames/3;});++fast;}CHECK(fast==62);++cases;
    call={203,args};calendar.dispatch(call);for(int n=0;n<5;++n)calendar.update([](auto){return 0;});CHECK(!calendar.busy());++cases;
    const auto before=calendar.checkpoint();auto bad=saved;bad.back()^=1;rejects([&]{calendar.restore(bad);});CHECK(calendar.checkpoint()==before);++guards;
    args[0]=s::Value(2009);call={203,args};rejects([&]{calendar.dispatch(call);});CHECK(calendar.checkpoint()==before);++guards;
    args[0]=s::Value(2010);args[3]=s::Value(7);call={203,args};rejects([&]{calendar.dispatch(call);});CHECK(calendar.checkpoint()==before);++guards;
    args={s::Value(201),s::Value(1),s::Value(29),s::Value(-1),s::Value(1)};call={203,args};CHECK(!calendar.dispatch(call)->complete && calendar.date()==(s::CalendarDate{201,1,29,4,false}));calendar.update();const auto literal_year=calendar.checkpoint();s::SceneCalendar literal_restore(loader);literal_restore.restore(literal_year);CHECK(literal_restore.date()==calendar.date() && literal_restore.checkpoint()==literal_year);for(unsigned n=0;calendar.busy();++n){CHECK(n<200);calendar.update();literal_restore.update();CHECK(literal_restore.checkpoint()==calendar.checkpoint());}++cases;
    args={s::Value(2013),s::Value(3),s::Value(1),s::Value(7),s::Value(0)};call={203,args};CHECK(calendar.dispatch(call)->complete && calendar.date().weekday==7 && !calendar.busy());++cases;
    args={s::Value(2013),s::Value(2),s::Value(29),s::Value(-1),s::Value(1)};call={203,args};CHECK(!calendar.dispatch(call)->complete && calendar.date()==(s::CalendarDate{2013,2,29,5,false}));calendar.update();const auto source_label=calendar.checkpoint();literal_restore.restore(source_label);CHECK(literal_restore.checkpoint()==source_label && literal_restore.date().day==29);++cases;
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.native-calendar-state.v1\",\"cases\":"<<cases<<",\"guards\":"<<guards<<",\"normal_ticks\":182,\"skip_ticks\":62,\"synthetic_only\":true,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";out.close();CHECK(out);
    std::cout<<"PASS native calendar date, atlas placement and transition restore.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
