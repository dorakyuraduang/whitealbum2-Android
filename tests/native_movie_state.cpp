#include "application.hpp"
#include "render_frame.hpp"
#include "native_scene_fixture.hpp"
#include <cmath>
#include <limits>

namespace m=wa2::movie;namespace a=wa2::audio;
struct MovieState {std::string name;int record{};bool done{},stopped{},paused{};unsigned updates{},destroyed{};float amplitude{};m::VideoFrame frame;};
struct Movie:m::Playback {
    std::shared_ptr<MovieState> state;explicit Movie(std::shared_ptr<MovieState> value):state(std::move(value)){}~Movie(){++state->destroyed;}
    bool finished()const override{return state->done;}void stop()override{state->stopped=state->done=true;}void update(std::int64_t)override{++state->updates;}
    void pause(bool value,std::int64_t)override{state->paused=value;}void gain(float value)override{state->amplitude=value;}std::optional<m::VideoFrame> frame()const override{return state->frame.owner?std::optional(state->frame):std::nullopt;}
};
struct VoiceState {int kind{},id{};bool loop{},paused{},stopped{};std::uint8_t gain{};};
struct Voice:a::Voice {std::shared_ptr<VoiceState> state;explicit Voice(std::shared_ptr<VoiceState> value):state(std::move(value)){}bool ready()const override{return true;}bool finished()const override{return state->stopped;}std::string failure()const override{return {};}void gain(std::uint8_t value)override{state->gain=value;}void stop()override{state->stopped=true;}void pause(bool value)override{state->paused=value;}};
struct Settings:g::PreferenceStorage {std::optional<Bytes> value;std::optional<Bytes> read()override{return value;}void replace(std::span<const std::uint8_t> bytes)override{value=Bytes(bytes.begin(),bytes.end());}};
struct Saves:g::SaveStorage {std::map<unsigned,Bytes> values;std::optional<Bytes> read(unsigned slot)override{const auto found=values.find(slot);return found==values.end()?std::optional<Bytes>{}:found->second;}void replace(unsigned slot,std::span<const std::uint8_t> bytes)override{values[slot]=Bytes(bytes.begin(),bytes.end());}std::vector<g::SaveSlotInfo> list()override{return {};}};
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{},guards{},prepares{};std::vector<std::shared_ptr<MovieState>> movies;std::vector<int> seen;
    const auto make_movie=[&](std::string_view name,int record)->std::unique_ptr<m::Playback>{auto state=std::make_shared<MovieState>();state->name=name;state->record=record;movies.push_back(state);return std::make_unique<Movie>(state);};
    s::SceneMovie controller(make_movie,[&]{++prepares;},0,[&](auto record){seen.push_back(record);});controller.gain(.5f);
    std::array<s::Value,2> args{s::Value(std::string("mv12")),s::Value(201)};s::Command command{193,args};CHECK(!controller.dispatch(command)->complete && command.continuation==1 && prepares==1 && movies.size()==1 && movies.back()->name=="mv120.pak" && movies.back()->record==201 && movies.back()->amplitude==.5f);++cases;
    CHECK(!controller.dispatch(command)->complete && prepares==1 && movies.size()==1);++cases;controller.gain(.25f);CHECK(movies.back()->amplitude==.25f);++cases;
    for(float value:{-1.f,1.01f,std::numeric_limits<float>::quiet_NaN()}){rejects([&]{controller.gain(value);});CHECK(movies.back()->amplitude==.25f);++guards;}++cases;
    controller.pause(true,0);CHECK(movies.back()->paused);controller.pause(false,0);controller.update(1);CHECK(!movies.back()->paused && movies.back()->updates==1);++cases;
    controller.stop();controller.stop();CHECK(movies.back()->stopped && seen==std::vector<int>{201});CHECK(controller.dispatch(command)->complete && !controller.playback() && movies.back()->destroyed==1);++cases;
    args[1]=s::Value(202);command={193,args};controller.dispatch(command);movies.back()->done=true;CHECK(controller.dispatch(command)->complete && seen==std::vector<int>({201,202}) && movies.back()->destroyed==1);++cases;
    const auto old=movies.size();args[1]=s::Value(1024);command={193,args};rejects([&]{controller.dispatch(command);});CHECK(movies.size()==old && prepares==2);++guards;++cases;
    args[1]=s::Value(203);for(const auto& name:std::array<std::string,3>{"../mv00","",std::string(121,'a')}){args[0]=s::Value(name);command={193,args};rejects([&]{controller.dispatch(command);});CHECK(movies.size()==old && prepares==2);++guards;}++cases;
    command={193,{}};rejects([&]{controller.dispatch(command);});++guards;command={194,{}};CHECK(!controller.dispatch(command));++cases;
    rejects([&]{s::SceneMovie invalid(make_movie,{},3);});rejects([&]{s::SceneMovie invalid({});});guards+=2;
    {s::SceneMovie null([](auto,auto)->std::unique_ptr<m::Playback>{return {};},[&]{++prepares;});args={s::Value(std::string("mv00")),s::Value(0)};command={193,args};rejects([&]{null.dispatch(command);});CHECK(!null.playback() && !command.continuation && prepares==3);++guards;++cases;}
    {s::SceneMovie alternate(make_movie,{},2);command={193,args};alternate.dispatch(command);CHECK(movies.back()->name=="mv002.pak" && movies.back()->amplitude==1);++cases;}CHECK(movies.back()->stopped && movies.back()->destroyed==1);++cases;
    {s::SceneMovie changing(make_movie);command={193,args};changing.dispatch(command);const auto active=movies.back();changing.file_suffix(1);CHECK(active->name=="mv000.pak" && !active->stopped);rejects([&]{changing.file_suffix(3);});++guards;CHECK(!changing.dispatch(command)->complete && movies.back()==active);active->done=true;CHECK(changing.dispatch(command)->complete);command={193,args};changing.dispatch(command);CHECK(movies.back()->name=="mv001.pak" && active->destroyed==1);++cases;}
    g::Preferences prefs;CHECK(prefs.movie_amplitude()==102/255.f);prefs.master=prefs.music=256;CHECK(prefs.movie_amplitude()==1);prefs.master=0;CHECK(prefs.movie_amplitude()==0);++cases;
    auto banks=memory_banks();Script script;
    script.call(146,{s::Value(1),s::Value(1),s::Value(0),s::Value(8),s::Value(0),s::Value(0),s::Value(0),s::Value(1280.f),s::Value(720.f)});
    script.call(176,{s::Value(0),s::Value(std::string("atlas.tga")),s::Value(24),s::Value(1),s::Value(std::string()),s::Value(256),s::Value(std::string("grp")),s::Value(1)});
    script.call(133,{s::Value(std::string()),s::Value(7),s::Value(1)});script.call(158,{s::Value(31),s::Value(0),s::Value(0),s::Value(255)});
    script.call(164,{s::Value(10),s::Value(255)});script.call(165,{s::Value(3),s::Value(11),s::Value(0),s::Value(1),s::Value(220),s::Value(0)});script.call(165,{s::Value(7),s::Value(12),s::Value(0),s::Value(1),s::Value(220),s::Value(1)});
    script.call(138,{s::Value(1),s::Value(255),s::Value(1),s::Value(0),s::Value(7)});script.call(139,{s::Value(2),s::Value(1001),s::Value(1),s::Value(200),s::Value(8),s::Value(1)});
    script.call(194,{s::Value(80),s::Value(0),s::Value(0)});script.call(193,{s::Value(std::string("mv12")),s::Value(201)});script.call(194,{s::Value(1000),s::Value(0),s::Value(0)});script.finish();
    banks["ck-gal.pak"]=bank({{"Global.vrb",text("3,FLG_00,0\r\n")},{"LFScriptFunc.fnc",functions(0,200)},{"LFScriptFuncEx.fnc",functions(200,239)},{"1001.bnr",script.code},{"1001.txt",text(script.strings)}});
    const r::BankOpener open=[&](auto name)->std::shared_ptr<r::Source>{const auto found=banks.find(std::string(name));return found==banks.end()?nullptr:found->second;};
    std::vector<std::shared_ptr<VoiceState>> sounds;g::SceneServices services;
    const auto make_voice=[&](int kind,int id,bool loop,bool paused)->std::unique_ptr<a::Voice>{auto state=std::make_shared<VoiceState>(VoiceState{kind,id,loop,paused});sounds.push_back(state);return std::make_unique<Voice>(state);};
    services.music=[&](auto id,bool loop){return make_voice(0,id,loop,false);};services.effects=[&](auto id,bool loop,bool paused){return make_voice(1,id,loop,paused);};services.speech=[&](const auto& line){return make_voice(2,line.line,line.loop,false);};
    unsigned game_movies{};services.movie=[&](auto name,auto record){CHECK(sounds.size()==6 && !sounds[0]->stopped && sounds[1]->stopped && sounds[2]->stopped && sounds[3]->stopped && sounds[4]->stopped && !sounds[5]->stopped);++game_movies;return make_movie(name,record);};
    auto progress=std::make_shared<p::Store>(std::make_shared<Storage>());auto preferences=std::make_shared<Settings>();auto saves=std::make_shared<Saves>();g::Application app(open,progress,preferences,saves,memory_resolver(),17,services);app.start({},0);std::int64_t now{};
    const auto step=[&]{now+=16667;app.frame(now,{},[&]{return now;});};const auto until=[&](auto predicate){const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(4);unsigned steps{};while(!predicate()){CHECK(++steps<300 && std::chrono::steady_clock::now()<deadline);step();std::this_thread::sleep_for(std::chrono::milliseconds(1));}};
    until([&]{return app.scene()->statistics().command==194;});for(unsigned n=0;n<15;++n)step();CHECK(sounds.size()==6 && sounds[0]->kind==0 && sounds[3]->paused && sounds[4]->id==7 && sounds[5]->id==8 && std::none_of(sounds.begin(),sounds.end(),[](auto state){return state->stopped;}));++cases;
    g::RenderFrame before;before.capture(app);CHECK(before.background().current && before.overlays().size()>=13 && !before.movie_active());const auto background_image=before.background().current->image,bitmap_image=before.overlays().front().image;const auto dialogue=app.scene()->dialogue_state().raw_text();++cases;
    CHECK(app.user_command(g::UserCommand::movie_low,now) && app.preferences().movie_quality==1 && (app.user_menu().checked&g::command_bit(g::UserCommand::movie_low)));until([&]{return app.scene()->movie_active();});const auto active=movies.back();CHECK(game_movies==1 && active->amplitude==102/255.f && active->name=="mv121.pak" && active->record==201 && !progress->state().flag(201));++cases;
    g::RenderFrame during;during.capture(app);CHECK(during.movie_active() && !during.movie() && !during.background().current && during.overlays().empty());CHECK(app.scene()->background().current->image==background_image && app.scene()->foreground().front().image==bitmap_image && app.scene()->dialogue_state().raw_text()==dialogue);++cases;
    CHECK(app.user_command(g::UserCommand::movie_high,now) && app.preferences().movie_quality==0 && active->name=="mv121.pak" && !active->stopped);prefs=app.preferences();prefs.master=64;prefs.music=128;app.preferences(prefs);CHECK(active->amplitude==32/255.f && !sounds[0]->stopped && !sounds[5]->stopped);++cases;
    rejects([&]{app.save(0);});rejects([&]{app.open(g::Page::options,now);});CHECK(app.page()==g::Page::story && app.scene()->movie_active());guards+=2;++cases;
    const auto updates=active->updates;app.background(true,now);step();CHECK(active->paused && active->updates==updates && app.mixer()->paused());app.background(false,now);step();CHECK(!active->paused && active->updates==updates+1 && !app.mixer()->paused());++cases;
    active->done=true;step();CHECK(!app.scene()->movie_active() && active->destroyed==1 && progress->state().flag(201)==1 && game_movies==1);step();CHECK(app.scene()->statistics().command==194);++cases;
    g::RenderFrame after;after.capture(app);CHECK(!after.movie_active() && after.background().current->image==background_image && after.overlays().front().image==bitmap_image && after.overlays().size()==before.overlays().size());++cases;
    const auto saved=app.scene()->checkpoint();app.save(4);CHECK(saves->values.at(4)==saved);++cases;
    app.load(4,now);CHECK(sounds.size()==8 && sounds[0]->stopped && sounds[5]->stopped && sounds[6]->kind==0 && sounds[7]->kind==2 && sounds[7]->id==8 && !sounds[6]->stopped && !sounds[7]->stopped && !app.scene()->movie_active());++cases;
    g::RenderFrame loaded;loaded.capture(app);CHECK(loaded.background().current && loaded.overlays().size()==before.overlays().size());step();CHECK(game_movies==1 && app.preferences()==prefs);++cases;
    app.close(now);CHECK(sounds[6]->stopped && sounds[7]->stopped);++cases;
    // A failed movie provider follows the same pre-play audio ordering. It
    // must leave no half-active transport or prematurely set the seen flag.
    s::SceneMovie failed([](auto,auto)->std::unique_ptr<m::Playback>{throw std::runtime_error("injected movie open failure");},[&]{++prepares;},0,[&](auto id){seen.push_back(id);});args={s::Value(std::string("mv12")),s::Value(204)};command={193,args};rejects([&]{failed.dispatch(command);});CHECK(!failed.playback() && command.continuation==0 && prepares==4 && seen==std::vector<int>({201,202}));++guards;++cases;
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.native-movie-state.v1\",\"cases\":"<<cases<<",\"guards\":"<<guards<<",\"source_preplay_audio_order\":true,\"movie_gain_follows_preferences\":true,\"visual_state_preserved\":true,\"services_survive_save_load\":true,\"fake_transports_only\":true,\"synthetic_resources_only\":true,\"original_resources_opened\":false,\"devices_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";CHECK(out);std::cout<<"PASS native movie gain, source pre-play audio order, background suspension and save/load service ownership.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
