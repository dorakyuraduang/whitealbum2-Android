#include "application.hpp"
#include "render_frame.hpp"
#include "audio_resources.hpp"
#include "native_scene_fixture.hpp"
#include <cmath>
#include <tuple>

namespace a=wa2::audio;
struct VoiceState {std::int32_t song{};bool loop{},stopped{};std::uint8_t gain{};};
struct Voice:a::Voice {std::shared_ptr<VoiceState> state;explicit Voice(std::shared_ptr<VoiceState> value):state(std::move(value)){}bool ready()const override{return true;}bool finished()const override{return state->stopped;}std::string failure()const override{return {};}void gain(std::uint8_t value)override{state->gain=value;}void stop()override{state->stopped=true;}};
struct MovieState {std::string name;bool done{},stopped{},paused{};unsigned updates{};float gain{};};
struct Movie:wa2::movie::Playback {std::shared_ptr<MovieState> state;explicit Movie(std::shared_ptr<MovieState> value):state(std::move(value)){}bool finished()const override{return state->done;}void stop()override{state->done=state->stopped=true;}void update(std::int64_t)override{++state->updates;}void pause(bool value,std::int64_t)override{state->paused=value;}void gain(float value)override{state->gain=value;}std::optional<wa2::movie::VideoFrame> frame()const override{return {};}};
struct Settings:g::PreferenceStorage {std::optional<Bytes> bytes;std::optional<Bytes> read()override{return bytes;}void replace(std::span<const std::uint8_t> b)override{bytes=Bytes(b.begin(),b.end());}};
struct Saves:g::SaveStorage {std::map<unsigned,Bytes> slots;std::optional<Bytes> read(unsigned slot)override{const auto it=slots.find(slot);return it==slots.end()?std::optional<Bytes>{}:it->second;}void replace(unsigned slot,std::span<const std::uint8_t> b)override{slots[slot]=Bytes(b.begin(),b.end());}std::vector<g::SaveSlotInfo> list()override{return {};}};
std::map<std::string,std::shared_ptr<Source>> title_banks(){
    auto banks=memory_banks();std::map<std::string,Bytes> pictures;const auto old=r::Archive::open(banks.at("grp.pak"));for(unsigned n=0;n<old.members().size();++n)pictures[old.members()[n].name]=old.load(n);
    constexpr std::string_view names[]{"T0000.tga","T0100.tga","T0010.tga","T0020.tga","T0050.tga","T0101.tga","T0200.tga","T0201.tga","T0011.tga"};constexpr unsigned widths[]{1280,640,752,264,408,320,288,288,752},heights[]{856,640,448,24,24,80,320,160,448};
    for(unsigned n=0;n<9;++n)pictures[std::string(names[n])]=raster(widths[n],heights[n]);banks["grp.pak"]=bank(std::move(pictures));return banks;
}
unsigned first(const std::shared_ptr<r::Source>& source){CHECK(source && source->size());std::uint8_t value{};source->read_at(0,{&value,1});return value;}
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{},guards{},lookup_cases{};
    {
        std::map<std::string,std::shared_ptr<Source>> banks;
        banks["se.pak"]=bank({{"SE_0007.ogg",{1}},{"SE_0007.wav",{2}},{"SE_0008.wav",{2}}});banks["IC/se.pak"]=bank({{"SE_0007.ogg",{3}},{"SE_0008.ogg",{3}},{"SE_0009.ogg",{3}},{"SE_0010.wav",{4}}});
        banks["voice.pak"]=bank({{"1001_0007_00.ogg",{5}}});banks["IC/voice.pak"]=bank({{"1001_0007_00.ogg",{6}},{"1001_0008_02.ogg",{6}}});
        banks["bgm.pak"]=bank({{"BGM_031.ogg",{7}},{"BGM_031_a.ogg",{8}},{"BGM_031_b.ogg",{9}},{"BGM_032_a.ogg",{8}},{"BGM_032_b.ogg",{9}},{"BGM_034_a.ogg",{8}}});banks["IC/bgm.pak"]=bank({{"BGM_031.ogg",{11}},{"BGM_032.ogg",{11}},{"BGM_033.ogg",{11}},{"BGM_034.ogg",{11}}});
        r::Catalog catalog([&](auto name)->std::shared_ptr<r::Source>{const auto found=banks.find(std::string(name));return found==banks.end()?nullptr:found->second;});
        for(const auto [id,expected,ogg]:std::array<std::tuple<int,int,bool>,4>{{{7,1,true},{8,2,false},{9,3,true},{10,4,false}}}){const auto effect=a::effect_resource(catalog,id);CHECK(effect.ogg==ogg && first(effect.source)==unsigned(expected));++lookup_cases;}
        CHECK(first(a::speech_resource(catalog,1001,7,0))==5 && first(a::speech_resource(catalog,1001,8,2))==6);lookup_cases+=2;
        for(bool loop:{false,true}){const auto music=a::music_resources(catalog,31,loop);CHECK(first(music.first)==7 && (loop?music.repeat==music.first:!music.repeat));++lookup_cases;const auto split=a::music_resources(catalog,32,loop);CHECK(first(split.first)==8 && (loop?first(split.repeat)==9:!split.repeat));++lookup_cases;}
        const auto fallback=a::music_resources(catalog,33,true);CHECK(first(fallback.first)==11 && fallback.repeat==fallback.first);++lookup_cases;CHECK(first(a::music_resources(catalog,34,false).first)==8);++lookup_cases;
        rejects([&]{a::music_resources(catalog,34,true);});rejects([&]{a::music_resources(catalog,-1,true);});rejects([&]{a::music_resources(catalog,200,true);});rejects([&]{a::music_resources(catalog,199,true);});rejects([&]{a::effect_resource(catalog,9999);});rejects([&]{a::speech_resource(catalog,1001,9,0);});guards+=6;
        bool used_fallback{};r::Catalog denied([&](auto name)->std::shared_ptr<r::Source>{if(name=="bgm.pak")throw std::runtime_error("injected resource permission error");used_fallback=true;return banks["IC/bgm.pak"];});rejects([&]{a::music_resources(denied,31,true);});CHECK(!used_fallback);++guards;
    }
    {
        const auto source=[](unsigned byte){auto value=std::make_shared<Source>();value->b={std::uint8_t(byte)};return value;};
        std::map<std::string,std::shared_ptr<Source>> files{{"mv000.pak",source(1)},{"IC/mv120.pak",source(2)},{"mv121.pak",source(3)},{"IC/mv131.pak",source(4)},{"mv130.pak",source(5)}};
        std::vector<std::string> opened;r::Catalog catalog([&](auto name)->std::shared_ptr<r::Source>{opened.emplace_back(name);const auto found=files.find(std::string(name));return found==files.end()?nullptr:found->second;});
        CHECK(first(catalog.movie("MV001.PAK"))==1 && opened==std::vector<std::string>({"mv001.pak","IC/mv001.pak","mv000.pak"}));++cases;
        opened.clear();CHECK(first(catalog.movie("mv000.pak"))==1 && first(catalog.movie("mv001.pak"))==1 && opened.empty());++cases;
        CHECK(first(catalog.movie("mv122.pak"))==2 && opened==std::vector<std::string>({"mv122.pak","IC/mv122.pak","mv120.pak","IC/mv120.pak"}));++cases;
        opened.clear();CHECK(first(catalog.movie("mv121.pak"))==3 && opened==std::vector<std::string>({"mv121.pak"}));++cases;
        opened.clear();CHECK(first(catalog.movie("mv131.pak"))==4 && opened==std::vector<std::string>({"mv131.pak","IC/mv131.pak"}));++cases;
        opened.clear();rejects([&]{catalog.movie("mv141.pak");});CHECK(opened==std::vector<std::string>({"mv141.pak","IC/mv141.pak","mv140.pak","IC/mv140.pak"}));++guards;++cases;
        for(const auto name:{"mv123.pak","clip1.pak","mv1.pak"}){opened.clear();rejects([&]{catalog.movie(name);});CHECK(opened==std::vector<std::string>({name,"IC/"+std::string(name)}));++guards;}++cases;
        for(const auto fail_at:{"mv001.pak","IC/mv001.pak","mv000.pak","IC/mv000.pak"}){std::vector<std::string> denied;r::Catalog errors([&](auto name)->std::shared_ptr<r::Source>{denied.emplace_back(name);if(name==fail_at)throw r::Error("owned movie permission failure");return {};});rejects([&]{errors.movie("mv001.pak");});CHECK(denied.back()==fail_at);++guards;}++cases;
        const auto damaged=source(0);r::Catalog exact_bad([&](auto name)->std::shared_ptr<r::Source>{return name=="mv001.pak"?damaged:files.at("mv000.pak");});CHECK(exact_bad.movie("mv001.pak")==damaged);++cases;
    }
    auto banks=title_banks();const r::BankOpener open=[&](auto name)->std::shared_ptr<r::Source>{const auto found=banks.find(std::string(name));return found==banks.end()?nullptr:found->second;};auto storage=std::make_shared<Storage>();auto progress=std::make_shared<p::Store>(storage);std::vector<std::shared_ptr<VoiceState>> voices;std::vector<std::shared_ptr<MovieState>> movies;
    g::TitleServices services;services.music=[&](auto song,bool loop)->std::unique_ptr<a::Voice>{auto state=std::make_shared<VoiceState>();state->song=song;state->loop=loop;voices.push_back(state);return std::make_unique<Voice>(state);};services.movie=[&](auto name)->std::unique_ptr<wa2::movie::Playback>{auto state=std::make_shared<MovieState>();state->name=name;movies.push_back(state);return std::make_unique<Movie>(state);};
    std::int64_t now=1000000;const auto tick=[&](g::TitleScene& title,g::TitleInput input={}){now+=16667;title.update(now,input);};const auto until=[&](auto& title,auto predicate){const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);unsigned steps{};while(!predicate()){CHECK(++steps<500 && std::chrono::steady_clock::now()<deadline);tick(title);std::this_thread::sleep_for(std::chrono::milliseconds(1));}};
    rejects([&]{g::TitleScene title(open,{},g::Preferences{},{});});rejects([&]{g::TitleScene title(open,progress,g::Preferences{}, {},{true,3});});guards+=2;
    {
        g::TitleScene title(open,progress,g::Preferences{}, {true,false,true},{},23,services);CHECK(title.stage()==g::TitleStage::company_movie && movies.back()->name=="mv000.pak" && std::abs(movies.back()->gain-.4f)<1e-7f && title.sprites().empty());++cases;
        tick(title);const auto frame=movies.back()->updates;title.pause(true,now);CHECK(title.mixer()->paused() && movies.back()->paused);tick(title);CHECK(movies.back()->updates==frame);title.pause(false,now);tick(title);CHECK(!title.mixer()->paused() && !movies.back()->paused && movies.back()->updates==frame+1);++cases;
        g::RenderFrame movie_frame;movie_frame.capture(title);CHECK(movie_frame.movie_active() && !movie_frame.movie() && movie_frame.overlays().empty());++cases;
        movies.back()->done=true;tick(title);CHECK(title.stage()==g::TitleStage::view && title.view().loading());++cases;
        until(title,[&]{return title.view().phase()==g::TitlePhase::logo_in;});tick(title);CHECK(voices.back()->song==31 && !voices.back()->loop && voices.back()->gain==101 && progress->state().flag(131)==1 && storage->writes>0);++cases;
        g::TitleInput skip;skip.selection.confirm=true;tick(title,skip);CHECK(title.view().phase()==g::TitlePhase::menu && !title.take_command());tick(title);const auto elapsed=title.view().elapsed(),seed=title.random_state();title.pause(true,now);for(unsigned n=0;n<3;++n)tick(title);CHECK(title.view().elapsed()==elapsed && title.random_state()==seed);title.pause(false,now);tick(title);++cases;
        g::RenderFrame title_frame;title_frame.capture(title);CHECK(!title_frame.movie_active() && title_frame.overlays().size()==title.sprites().size() && !title_frame.overlays().empty() && title_frame.overlays()[0].image==title.sprites()[0].image);++cases;
        g::Preferences muted;muted.music=0;title.preferences(muted);CHECK(voices.back()->gain==0);++cases;
        rejects([&]{title.update(now-1);});++guards;
    }
    CHECK(voices.back()->stopped);++cases;
    progress->state().set_flag(202,1);
    {
        g::TitleScene title(open,progress,g::Preferences{}, {true,false,true},{true,1},5,services);CHECK(movies.back()->name=="mv001.pak");g::TitleInput skip;skip.selection.activate=true;tick(title,skip);CHECK(movies.back()->stopped && title.stage()==g::TitleStage::opening_delay);const auto count=movies.size();for(unsigned n=0;n<3;++n)tick(title);CHECK(movies.size()==count);tick(title);CHECK(title.stage()==g::TitleStage::opening_movie && movies.back()->name=="mv021.pak" && progress->state().flag(104)==1);movies.back()->done=true;tick(title);CHECK(title.stage()==g::TitleStage::view);++cases;
    }
    progress->state().set_flag(210,1);
    for(bool final:{false,true}){progress->state().set_flag(220,final?1:0);g::TitleScene title(open,progress,g::Preferences{}, {true,false,true},{},5,services);movies.back()->done=true;tick(title);for(unsigned n=0;n<4;++n)tick(title);CHECK(movies.back()->name==(final?"mv200.pak":"mv100.pak"));++cases;}
    auto bad=services;bad.movie=[](auto)->std::unique_ptr<wa2::movie::Playback>{return {};};rejects([&]{g::TitleScene title(open,progress,g::Preferences{}, {},{},1,bad);});++guards;
    auto settings=std::make_shared<Settings>();auto saves=std::make_shared<Saves>();g::Application app(open,progress,settings,saves,memory_resolver(),42);app.prepare_title({true,false,true},{false,0},services);auto* owner=app.title_scene();auto mixer=app.mixer();CHECK(owner && mixer && !app.scene() && app.page()==g::Page::title);++cases;
    const auto frame=[&](g::Input input={}){now+=16667;return app.frame(now,input,[&]{return now;});};const auto wait=[&](auto predicate){const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);unsigned steps{};while(!predicate()){CHECK(++steps<500 && std::chrono::steady_clock::now()<deadline);frame();std::this_thread::sleep_for(std::chrono::milliseconds(1));}};
    wait([&]{return owner->view().phase()==g::TitlePhase::logo_in;});g::Input confirm;confirm.advance=true;CHECK(frame(confirm).ticks==0 && owner->view().phase()==g::TitlePhase::menu && app.page()==g::Page::title);frame();++cases;
    g::Input click;click.selection={true,true,false,640,441,0};frame(click);CHECK(app.page()==g::Page::options && app.title_scene()==owner && app.mixer()==mixer);g::Input cancel;cancel.cancel=true;frame(cancel);CHECK(app.page()==g::Page::title && owner->view().phase()==g::TitlePhase::menu);++cases;
    click.selection.y=401;frame(click);CHECK(app.page()==g::Page::load);rejects([&]{app.load(0,now);});CHECK(app.title_scene()==owner && app.mixer()==mixer && app.page()==g::Page::load);CHECK(app.back(now));++guards;++cases;
    rejects([&]{app.prepare_title({}, {true,3},services);});CHECK(app.title_scene()==owner && app.mixer()==mixer);++guards;
    const auto elapsed=owner->view().elapsed();app.background(true,now);frame();CHECK(mixer->paused() && owner->view().elapsed()==elapsed);app.background(false,now);frame();CHECK(!mixer->paused());++cases;
    const auto seed=owner->random_state();app.start({},now);CHECK(app.page()==g::Page::story && app.scene() && !app.title_scene() && app.scene()->random_state()==seed && app.mixer()!=mixer && voices.back()->stopped);++cases;
    const auto next_seed=app.scene()->random_state();app.title(now);CHECK(app.title_scene() && app.title_scene()->random_state()==next_seed && app.page()==g::Page::title && app.mixer()!=mixer);wait([&]{return app.title_scene()->view().phase()==g::TitlePhase::logo_in;});frame(confirm);frame();click.selection.y=361;frame(click);CHECK(app.title_scene()->view().phase()==g::TitlePhase::story_out);for(unsigned n=0;n<45;++n)frame();CHECK(app.page()==g::Page::story && app.scene() && !app.title_scene());++cases;
    app.title(now);wait([&]{return app.title_scene()->view().phase()==g::TitlePhase::logo_in;});frame(confirm);frame();click.selection.y=561;frame(click);for(unsigned n=0;n<120;++n)frame();CHECK(app.page()==g::Page::closed && !app.mixer() && !app.title_scene());++cases;
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.native-title-scene.v1\",\"cases\":"<<cases<<",\"guards\":"<<guards<<",\"audio_lookup_cases\":"<<lookup_cases<<",\"missing_movie_quality_uses_default\":true,\"exact_quality_precedes_fallback\":true,\"movie_permission_errors_preserved\":true,\"synthetic_resources_only\":true,\"fake_music_and_movies\":true,\"title_story_handoff\":true,\"devices_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";CHECK(out);std::cout<<"PASS native title movies, shared audio resources, application transitions and suspension.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
