#include "preferences.hpp"
#include "preferences_ini.hpp"
#include "scene_voices.hpp"
#include "checkpoint_codec.hpp"
#include <fstream>
#include <iostream>

namespace g=wa2::game;namespace s=wa2::script;namespace a=wa2::audio;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+ #x);}while(false)
template<class F>void rejects(F&& f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}CHECK(caught);}
using Bytes=std::vector<std::uint8_t>;
struct Storage:g::PreferenceStorage {std::optional<Bytes> bytes;std::optional<g::Preferences> imported;unsigned writes{},imports{};bool failing{};std::optional<Bytes> read()override{return bytes;}std::optional<g::Preferences> legacy()override{++imports;return imported;}void replace(std::span<const std::uint8_t> b)override{if(failing)throw std::runtime_error("injected preference write failure");bytes=Bytes(b.begin(),b.end());++writes;}};
struct State {s::SpokenLine line;unsigned gain{},stops{};};
struct Voice:a::Voice {std::shared_ptr<State> state;explicit Voice(std::shared_ptr<State> v):state(std::move(v)){}bool ready()const override{return true;}bool finished()const override{return false;}std::string failure()const override{return {};}void gain(std::uint8_t v)override{state->gain=v;}void stop()override{++state->stops;}};
Bytes patch(Bytes bytes,unsigned at,unsigned value){for(unsigned n=0;n<4;++n)bytes.at(at+n)=std::uint8_t(value>>(8*n));const auto crc=wa2::checkpoint::crc(std::span(bytes).first(bytes.size()-4));for(unsigned n=0;n<4;++n)bytes[bytes.size()-4+n]=std::uint8_t(crc>>(8*n));return bytes;}
Bytes ini(std::string_view text){return Bytes(text.begin(),text.end());}
Bytes utf16(std::string_view text,bool little){Bytes result{std::uint8_t(little?255:254),std::uint8_t(little?254:255)};for(auto c:text){result.push_back(little?std::uint8_t(c):0);result.push_back(little?0:std::uint8_t(c));}return result;}
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{},guards{};auto memory=std::make_shared<Storage>();g::PreferenceStore settings(memory);g::Preferences defaults;CHECK(settings.current()==defaults && !memory->writes && !memory->bytes);++cases;
    CHECK(defaults.master==195 && defaults.music==134 && defaults.effects==195 && defaults.speech==195 && defaults.skin==0 && defaults.auto_ticks==137);++cases;
    CHECK(g::import_preferences_ini({},{})==defaults && g::import_preferences_ini(ini("; opaque ANSI comment\n[other]\nall_vol=0\n"),{})==defaults);++cases;
    const auto original=ini("[DEFAULT]\r\nall_vol=128\r\nbgm_vol=64\nse_vol=256\nvoice_vol=32\nall_sound=0\nbgm=1\nse=0\nvoice=0\nwin_color=3\nwin_alpha=0\nwin_alpha_vis=256\nwin_alpha_novel=96\nwin_alpha_command=128\nwin_disp=0\nyes_no=0\nR_click=2\npage_voice=1\nero_voice=1\nmsg_cut_optin=1\nauto_max=99\nmov_lv=1\nwait=0\nmsg_wait=1\nchar_voice0=0\nchar_voice9=0\nwheel=0\nmenu=0\n");
    const auto display=ini("[DEFAULT]\nwindow_mode=0\nWIN_SIZE_W=1024\nWIN_SIZE_H=576\nVSYNC=1\n");const auto imported=g::import_preferences_ini(original,display);
    CHECK(imported.master==128 && imported.music==64 && imported.effects==256 && imported.speech==32 && imported.audio_muted[0] && !imported.audio_muted[1] && imported.audio_muted[2] && !imported.voice_enabled);++cases;
    CHECK(imported.skin==3 && !imported.window_alpha && imported.picture_alpha==256 && imported.novel_alpha==96 && imported.button_alpha==128 && !imported.window && !imported.confirmation && imported.right_click==2);++cases;
    CHECK(imported.cut_voice_on_advance && imported.mute_adult && !imported.skip_read_only && imported.auto_ticks==99 && imported.movie_quality==1 && !imported.transition_wait && imported.text_step==4 && imported.fullscreen && imported.window_size==1 && !imported.character_voice[0] && imported.character_voice[1] && !imported.character_voice[9]);++cases;
    for(const auto& [value,step]:std::array<std::pair<int,int>,4>{{{0,0},{1,4},{2,2},{4,1}}}){CHECK(g::import_preferences_ini(ini("[default]\nmsg_wait="+std::to_string(value)),{}).text_step==step);}++cases;
    CHECK(g::import_preferences_ini(ini(" [ default ] \n ALL_VOL = \"+73\"\nall_vol=11\n ; comment\n[unrelated]\nall_vol=5"),{}).master==73);++cases;
    for(bool little:{false,true})CHECK(g::import_preferences_ini(utf16("[DEFAULT]\nall_vol=82\n",little),{}).master==82);CHECK(g::import_preferences_ini(ini("\xef\xbb\xbf[DEFAULT]\nall_vol=83\n"),{}).master==83);++cases;
    for(const auto& [dimensions,size]:std::array<std::pair<std::string_view,int>,2>{{{"WIN_SIZE_W=1280\nWIN_SIZE_H=720",0},{"WIN_SIZE_W=800\nWIN_SIZE_H=450",2}}})CHECK(g::import_preferences_ini({},ini(std::string("[DEFAULT]\n")+std::string(dimensions))).window_size==size);++cases;
    for(const auto& bad:{"all_vol=-1","voice_vol=257","win_color=10","win_alpha=257","msg_cut_optin=2","R_click=3","mov_lv=2","wait=1","msg_wait=3","wheel=1","voice=2","auto_max=-1","char_voice9=2","se_vol=1.5","bgm_vol=99999999999999999999","all_vol=17junk","all_vol=","all_vol","[DEFAULT"}){rejects([&]{g::import_preferences_ini(ini(std::string("[DEFAULT]\n")+bad),{});});++guards;}
    for(const auto& bad:{"window_mode=2","WIN_SIZE_W=800","WIN_SIZE_W=900\nWIN_SIZE_H=600"}){rejects([&]{g::import_preferences_ini({},ini(std::string("[DEFAULT]\n")+bad));});++guards;}
    for(auto bad:{Bytes(65537,';'),ini(std::string(4097,';')),ini(std::string(4097,'\n')),Bytes{255,254,65},Bytes{255,254,0,216},Bytes{255,254,0,220},Bytes{0},Bytes{254,255,0,0}}){rejects([&]{g::import_preferences_ini(bad,{});});++guards;}
    auto migrated=std::make_shared<Storage>();migrated->imported=imported;g::PreferenceStore first_import(migrated);CHECK(first_import.current()==imported && migrated->writes==1 && migrated->imports==1);migrated->imported=defaults;g::PreferenceStore already_imported(migrated);CHECK(already_imported.current()==imported && migrated->writes==1 && migrated->imports==1);++cases;
    migrated=std::make_shared<Storage>();migrated->imported=imported;migrated->failing=true;rejects([&]{g::PreferenceStore failed_import(migrated);});CHECK(!migrated->bytes && !migrated->writes && migrated->imports==1);++guards;
    auto value=defaults;value.master=256;value.music=0;value.skin=9;value.text_step=0;value.auto_ticks=36000;value.window=false;value.skip_read_only=false;value.mute_adult=true;value.voice_enabled=false;value.captions=false;value.character_voice[8]=false;CHECK(g::Preferences::decode(value.encode())==value);++cases;
    CHECK(settings.replace(value) && memory->writes==1 && settings.current()==value);CHECK(!settings.replace(value) && memory->writes==1);g::PreferenceStore reopened(memory);CHECK(reopened.current()==value);++cases;
    memory->failing=true;rejects([&]{settings.replace(defaults);});CHECK(settings.current()==value && g::Preferences::decode(*memory->bytes)==value && memory->writes==1);memory->failing=false;++guards;
    for(unsigned field=0;field<11;++field){const auto bytes=patch(defaults.encode(),12+field*4,0xffffffff);rejects([&]{g::Preferences::decode(bytes);});++guards;}
    for(unsigned field=0;field<15;++field){const auto bytes=patch(defaults.encode(),12+44+field*4,2);rejects([&]{g::Preferences::decode(bytes);});++guards;}
    auto bytes=value.encode();bytes.back()^=1;rejects([&]{g::Preferences::decode(bytes);});++guards;bytes=value.encode();bytes[4]=2;rejects([&]{g::Preferences::decode(bytes);});++guards;bytes=value.encode();bytes.pop_back();rejects([&]{g::Preferences::decode(bytes);});++guards;
    auto invalid=value;invalid.speech=257;rejects([&]{settings.replace(invalid);});CHECK(settings.current()==value && memory->writes==1);++guards;memory->bytes=bytes;rejects([&]{g::PreferenceStore corrupt(memory);});++guards;rejects([&]{g::PreferenceStore missing({});});++guards;
    std::vector<std::shared_ptr<State>> made;bool skip{};unsigned issued{};
    const auto factory=[&](const s::SpokenLine& line){auto state=std::make_shared<State>();state->line=line;made.push_back(state);return std::make_unique<Voice>(state);};
    s::SceneVoices voice(factory,[]{return 1001;},[]{return 0;},{},[&]{return skip;},{},{{},{},[&](auto,const auto&,auto){++issued;}});
    const auto play=[&](int speaker,int loop=1){std::array<s::Value,6> args{s::Value(speaker),s::Value(1001),s::Value(loop),s::Value(255),s::Value(1),s::Value(0)};s::Command command{139,args};voice.dispatch(command);};
    play(2);auto old=made.back();CHECK(voice.active()==1 && old->gain==255 && issued==1);++cases;
    std::array<bool,10> switches;switches.fill(true);switches[1]=false;voice.character_switches(switches);const auto count=made.size();play(2);CHECK(made.size()==count && !old->stops && voice.active()==1 && issued==2);++cases;
    skip=true;play(1,0);CHECK(made.size()==count && !old->stops);skip=false;++cases;
    voice.adult_mode(true);voice.settings(256,256,true);play(0);CHECK(made.size()==count && !old->stops);voice.adult_mode(false);++cases;
    switches[8]=switches[9]=false;voice.character_switches(switches);for(int id:{12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,98,99})play(id);CHECK(made.size()==count && !old->stops);++cases;
    play(38);CHECK(made.size()==count+1 && old->stops==1 && made.back()->line.speaker==38);old=made.back();++cases;
    voice.enabled(false);voice.update();CHECK(!old->stops && old->gain==0);play(38);CHECK(made.back()==old);voice.enabled(true);voice.update();CHECK(made.back()==old && old->gain==0);++cases;
    voice.settings(128,128);play(38);CHECK(made.back()->gain==63 && old->stops==1);++cases;
    auto saved=voice.checkpoint();voice.enabled(false);const auto before=made.size();voice.restore(saved);CHECK(made.size()==before && voice.active()==0);++cases;
    voice.enabled(true);switches[1]=true;voice.character_switches(switches);play(13);CHECK(made.size()==before);switches[8]=true;voice.character_switches(switches);play(13);CHECK(made.size()==before+1);saved=voice.checkpoint();switches[8]=false;voice.character_switches(switches);voice.restore(saved);CHECK(made.size()==before+1 && voice.active()==0);++cases;
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.native-preferences.v1\",\"cases\":"<<cases<<",\"guards\":"<<guards<<",\"memory_storage_only\":true,\"fake_voices_only\":true,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";CHECK(out);std::cout<<"PASS native preferences, failed-write isolation and original character voice switches.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
