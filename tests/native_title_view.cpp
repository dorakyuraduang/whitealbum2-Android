#include "title_view.hpp"
#include "scene_geometry.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <thread>

namespace g=wa2::game;namespace r=wa2::resource;namespace i=wa2::image;namespace p=wa2::progress;namespace a=wa2::audio;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
template<class F>void rejects(F&& f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}CHECK(caught);}
void put(std::vector<std::uint8_t>& b,std::size_t at,std::uint32_t v){for(unsigned n=0;n<4;++n)b.at(at+n)=std::uint8_t(v>>(8*n));}
struct Bytes:r::Source {std::vector<std::uint8_t> b;std::uint64_t size()const override{return b.size();}void read_at(std::uint64_t at,std::span<std::uint8_t> out)override{CHECK(at<=b.size() && out.size()<=b.size()-at);std::copy_n(b.data()+at,out.size(),out.data());}};
std::shared_ptr<Bytes> fixture(){
    constexpr std::string_view names[]{"T0000.tga","T0100.tga","T0010.tga","T0020.tga","T0050.tga","T0101.tga","T0200.tga","T0201.tga","T0011.tga","weather.tga"};
    constexpr unsigned widths[]{1280,640,752,264,408,320,288,288,752,32},heights[]{856,640,448,24,24,80,320,160,448,64};
    auto source=std::make_shared<Bytes>();auto& b=source->b;b.resize(16+44*10);std::memcpy(b.data(),"KCAP",4);put(b,12,10);
    for(unsigned n=0;n<10;++n){const auto at=b.size();const auto size=18+widths[n]*heights[n]*3;std::memcpy(b.data()+20+n*44,names[n].data(),names[n].size());put(b,52+n*44,unsigned(at));put(b,56+n*44,unsigned(size));b.resize(at+size,127);b[at]=b[at+1]=0;b[at+2]=2;std::fill_n(b.data()+at+3,9,0);b[at+12]=std::uint8_t(widths[n]);b[at+13]=std::uint8_t(widths[n]>>8);b[at+14]=std::uint8_t(heights[n]);b[at+15]=std::uint8_t(heights[n]>>8);b[at+16]=24;b[at+17]=32;}
    return source;
}
g::TitleInput pointer(float x,float y,bool click=false){g::TitleInput in;in.selection={true,click,false,x,y,0};return in;}
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{},guards{},loads{},draws{};bool missing{};std::vector<std::string> requested;std::vector<a::SystemCue> sounds;std::vector<std::pair<std::optional<std::int32_t>,std::int32_t>> music;
    auto source=fixture();i::ImageStore images(r::Archive::open(source));wa2::Random random(17);p::State progress;
    const g::TitleView::Loader load=[&](auto name){++loads;requested.emplace_back(name);if(missing && name=="T0201.tga")return std::shared_ptr<i::ImageRequest>{};return images.request(name);};
    g::TitleView view(load,[&]{++draws;return random.next();},[&](auto cue){sounds.push_back(cue);},[&](auto song,auto fade){music.emplace_back(song,fade);});
    const auto until=[&](auto predicate){const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);unsigned steps{};while(!predicate()){CHECK(++steps<1000 && std::chrono::steady_clock::now()<deadline);view.update();std::this_thread::sleep_for(std::chrono::milliseconds(1));}};
    const auto ready=[&](g::TitleContent content={true,false,true}){view.prepare(content,progress);until([&]{return view.phase()==g::TitlePhase::logo_in;});view.update(pointer(640,370,true));CHECK(view.phase()==g::TitlePhase::menu && !view.take_command());};
    const auto choose=[&](unsigned row){const auto found=std::find_if(view.rows().begin(),view.rows().end(),[&](const auto& item){return item.ordinal==row;});CHECK(found!=view.rows().end());const auto y=found->y;view.update(pointer(640,y+1,true));};
    rejects([&]{view.update();});++guards;CHECK(view.phase()==g::TitlePhase::finished && view.sprites().empty() && !loads);++cases;
    view.prepare({true,false,true},progress);CHECK(loads==8 && requested[2]=="T0011.tga" && view.loading() && view.sprites().empty() && !draws);++cases;
    until([&]{return view.phase()==g::TitlePhase::logo_in;});CHECK(view.elapsed()==0 && std::abs(view.pan()-37.200008f)<.0001f && music.empty() && !view.take_command());++cases;
    view.update();CHECK(view.elapsed()==1 && music.size()==1 && music[0].first==31 && music[0].second==0);
    auto logo=std::find_if(view.sprites().begin(),view.sprites().end(),[](const auto& sprite){return sprite.layer==0x104e && sprite.order==2;});CHECK(logo!=view.sprites().end() && logo->alpha==4 && logo->width>752 && logo->height>448);++cases;
    view.update(pointer(640,370,true));CHECK(view.phase()==g::TitlePhase::menu && view.pan()==136 && !view.take_command() && sounds.empty() && music.size()==1);++cases;
    CHECK(view.rows().size()==4 && view.rows()[0].ordinal==0 && view.rows()[3].ordinal==5);view.update(pointer(569.99f,370));CHECK(!view.focused());view.update(pointer(570,360));CHECK(view.focused()==0 && sounds.back()==a::SystemCue::focus);view.update(pointer(710,360));CHECK(!view.focused());view.update(pointer(640,392));CHECK(!view.focused());++cases;
    until([&]{return !view.weather().loading() && view.weather().extent()>0;});CHECK(view.weather().type()==3 && draws>0 && loads==9);
    for(unsigned n=0;n<view.sprites().size();++n){const auto& sprite=view.sprites()[n];CHECK(i::scene_quad({sprite.x,sprite.y,sprite.width,sprite.height,sprite.source_x,sprite.source_y,sprite.source_width,sprite.source_height},sprite.image->width,sprite.image->height));if(n)CHECK(view.sprites()[n-1].layer<=sprite.layer);}++cases;
    choose(2);CHECK(view.phase()==g::TitlePhase::modal);auto command=view.take_command();CHECK(command && command->kind==g::TitleCommand::Kind::options && !view.take_command());view.resume(progress);CHECK(view.phase()==g::TitlePhase::menu && view.elapsed()==64 && !view.focused());++cases;
    choose(1);rejects([&]{view.resume(progress);});++guards;command=view.take_command();CHECK(command && command->kind==g::TitleCommand::Kind::load);view.resume(progress);++cases;
    const auto before=view.sprites().size();missing=true;rejects([&]{view.prepare({},progress);});missing=false;CHECK(view.phase()==g::TitlePhase::menu && view.sprites().size()==before && view.weather().active());++guards;
    choose(0);CHECK(view.phase()==g::TitlePhase::story_out && !view.take_command());for(unsigned n=0;n<29;++n)view.update();CHECK(!view.take_command() && view.sprites()[0].red==10);view.update();CHECK(view.sprites()[0].red==0);for(unsigned n=30;n<45;++n)view.update();command=view.take_command();CHECK(view.phase()==g::TitlePhase::finished && command && command->kind==g::TitleCommand::Kind::start && command->scene=="1001" && view.sprites().empty() && !view.take_command());++cases;
    ready({false,false,false});CHECK(requested[requested.size()-6]=="T0010.tga");choose(0);for(unsigned n=0;n<45;++n)view.update();command=view.take_command();CHECK(command && command->scene=="2001");++cases;
    progress.set_flag(0,1);ready();choose(0);CHECK(view.section()==g::TitleSection::chapters && view.rows().size()==3 && view.rows()[0].cell==5 && view.rows()[1].cell==6 && view.rows()[2].ordinal==5);++cases;
    view.update(pointer(640,441,true));CHECK(view.phase()==g::TitlePhase::menu && !view.focused() && !view.take_command());view.update({{},true});CHECK(view.section()==g::TitleSection::root && sounds.back()==a::SystemCue::cancel);++cases;
    choose(0);choose(1);for(unsigned n=0;n<45;++n)view.update();command=view.take_command();CHECK(command && command->scene=="2001");++cases;
    progress.set_flag(5,1);ready();choose(0);CHECK(view.rows().size()==4);choose(2);for(unsigned n=0;n<45;++n)view.update();command=view.take_command();CHECK(command && command->scene=="3001");++cases;
    ready({false,false,true});choose(0);CHECK(view.rows().size()==3 && view.rows()[0].ordinal==1 && view.rows()[0].y==360 && view.rows()[1].ordinal==2 && view.rows()[1].y==400);choose(5);CHECK(view.section()==g::TitleSection::root && sounds.back()==a::SystemCue::cancel);++cases;
    ready();choose(3);CHECK(view.section()==g::TitleSection::extras && view.rows().size()==3 && view.rows()[0].ordinal==0 && view.rows()[1].ordinal==3);choose(0);CHECK(view.section()==g::TitleSection::special_stories && view.rows().size()==3);choose(1);for(unsigned n=0;n<45;++n)view.update();command=view.take_command();CHECK(command && command->scene=="5100");++cases;
    progress.set_flag(3,1);for(unsigned n:{20,21,22,23,24,25,30,31,32,33})progress.set_flag(n,1);ready();choose(3);CHECK(view.rows().size()==6);choose(4);command=view.take_command();CHECK(command && command->kind==g::TitleCommand::Kind::extra && command->extra==4);view.resume(progress);CHECK(view.section()==g::TitleSection::extras);view.update({{},true});CHECK(view.section()==g::TitleSection::root);++cases;
    choose(3);choose(0);choose(5);CHECK(view.section()==g::TitleSection::extras);choose(5);choose(5);CHECK(view.phase()==g::TitlePhase::quit_out);for(unsigned n=0;n<16;++n)view.update();for(const auto& sprite:view.sprites())if(sprite.layer>=0x104e)CHECK(!sprite.alpha);for(unsigned n=16;n<120;++n)view.update();command=view.take_command();CHECK(command && command->kind==g::TitleCommand::Kind::quit && music.back().second==30 && !music.back().first && view.sprites().empty());++cases;
    rejects([&]{view.resume(progress);});++guards;g::TitleInput invalid;invalid.selection.navigation=2;rejects([&]{view.update(invalid);});++guards;invalid={};invalid.selection.x=std::numeric_limits<float>::infinity();rejects([&]{view.update(invalid);});++guards;
    progress.set_flag(20,2);ready();choose(3);CHECK(view.rows().size()==5 && std::none_of(view.rows().begin(),view.rows().end(),[](const auto& row){return row.ordinal==4;}));++cases;
    view.clear();CHECK(view.sprites().empty() && !view.weather().active() && view.phase()==g::TitlePhase::finished);++cases;
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.native-title-view.v1\",\"cases\":"<<cases<<",\"guards\":"<<guards<<",\"synthetic_images_only\":true,\"original_resources_opened\":false,\"devices_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";CHECK(out);std::cout<<"PASS shared native title fades, hit regions, content access and navigation.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
