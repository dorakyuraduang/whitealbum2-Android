#include "application.hpp"
#include "confirmation_view.hpp"
#include "options_audio.hpp"
#include "render_frame.hpp"
#include "scene_geometry.hpp"
#include "native_scene_fixture.hpp"
#include "native_options_atlases.hpp"
#include <algorithm>
#include <limits>

namespace a=wa2::audio;namespace t=wa2::text;
struct Settings:g::PreferenceStorage {
    std::optional<Bytes> bytes;bool fail{};unsigned writes{};
    std::optional<Bytes> read()override{return bytes;}
    void replace(std::span<const std::uint8_t> data)override{if(fail)throw std::runtime_error("owned preference write failed");bytes=Bytes(data.begin(),data.end());++writes;}
};
struct Saves:g::SaveStorage {std::optional<Bytes> read(unsigned)override{return {};}void replace(unsigned,std::span<const std::uint8_t>)override{}std::vector<g::SaveSlotInfo> list()override{return {};}};
struct VoiceState {unsigned stops{},gain{};bool loop{};int id{};s::SpokenLine line;};
struct Voice:a::Voice {
    std::shared_ptr<VoiceState> state;explicit Voice(std::shared_ptr<VoiceState> value):state(std::move(value)){}
    bool ready()const override{return true;}bool finished()const override{return state->stops!=0;}std::string failure()const override{return {};}
    void gain(std::uint8_t value)override{state->gain=value;}void stop()override{++state->stops;}
};
g::ConfirmationInput pointer(float x,float y,bool activate=false){return {{true,activate,false,x,y,0},false};}
template<class Predicate,class Step>void until(Predicate predicate,Step step){const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);unsigned n{};while(!predicate()){CHECK(++n<2000 && std::chrono::steady_clock::now()<deadline);step();std::this_thread::sleep_for(std::chrono::milliseconds(1));}}
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{},guards{},labels{},requested{};bool missing{},text_failure{};
    std::map<std::string,Bytes> menu_images;for(const auto& atlas:wa2::test::options_atlases)menu_images[atlas.name]=raster(atlas.w,atlas.h);
    menu_images["sys_00100.tga"]=raster(544,216);menu_images["sys_10100.tga"]=raster(544,216);menu_images["sys_00101.tga"]=raster(320,120);
    i::ImageStore images(r::Archive::open(bank(std::move(menu_images))));
    const g::ConfirmationView::Loader loader=[&](auto name){++requested;if(missing)return std::shared_ptr<i::ImageRequest>{};return images.request(name);};
    const g::ConfirmationView::Text text=[&](auto raw){if(text_failure)throw std::runtime_error("owned text failure");CHECK(raw==g::options_reset_prompt());++labels;t::TextStyle style;style.size=28;style.columns=15;style.rows=2;style.line_gap=0;auto layout=t::layout(raw,style);CHECK(layout.pages.size()==1 && !layout.letters.empty());return t::FontBook::defer(std::move(layout));};
    std::vector<a::SystemCue> cues;
    rejects([&]{g::ConfirmationView bad({},text);});++guards;rejects([&]{g::ConfirmationView bad(loader,{});});++guards;
    g::ConfirmationView view(loader,text,[&](auto cue){cues.push_back(cue);});
    rejects([&]{view.update();});++guards;
    rejects([&]{view.prepare("");});++guards;rejects([&]{view.prepare(g::options_reset_prompt(),-1);});++guards;rejects([&]{view.prepare(g::options_reset_prompt(),10);});++guards;rejects([&]{view.prepare(g::options_reset_prompt(),0,601);});++guards;rejects([&]{view.prepare(g::options_reset_prompt(),0,15,601);});++guards;++cases;
    view.prepare(g::options_reset_prompt(),0,3,2);CHECK(view.phase()==g::ConfirmationPhase::loading && requested==1 && labels==1);++cases;
    until([&]{return view.phase()==g::ConfirmationPhase::opening;},[&]{view.update(pointer(473,377,true));});CHECK(!view.take_answer() && view.sprites().size()==4);++cases;
    CHECK(view.sprites()[0].alpha==85);for(const auto& sprite:view.sprites())CHECK(i::scene_quad({sprite.x,sprite.y,sprite.width,sprite.height,sprite.source_x,sprite.source_y,sprite.source_width,sprite.source_height},sprite.image->width,sprite.image->height));++cases;
    view.update(pointer(473,377,true));view.update(pointer(473,377,true));CHECK(view.phase()==g::ConfirmationPhase::active && !view.focused() && !view.take_answer());++cases;
    {const auto panel=std::find_if(view.sprites().begin(),view.sprites().end(),[](const auto& sprite){return sprite.order==0x533;});CHECK(panel!=view.sprites().end() && panel->source_width==656 && panel->source_height==264 && panel->width==656 && panel->height==264);
        const auto quad=i::scene_quad({panel->x,panel->y,panel->width,panel->height,panel->source_x,panel->source_y,panel->source_width,panel->source_height},panel->image->width,panel->image->height);CHECK(quad);float right{},bottom{};for(const auto& vertex:*quad){right=std::max(right,vertex.x);bottom=std::max(bottom,vertex.y);}CHECK(right==912 && bottom==472);
        for(const auto& sprite:view.sprites())if(sprite.order==0x534 || sprite.order==0x535)CHECK(sprite.x>=368 && sprite.x+sprite.width<=right && sprite.y>=256 && sprite.y+sprite.height<=bottom);
    }++cases;
    view.update({{false,false,true},false});CHECK(!view.take_answer());view.update(pointer(632,377,true));CHECK(!view.focused() && !view.take_answer());view.update(pointer(649,416,true));CHECK(!view.focused());++cases;
    view.update(pointer(649,377));CHECK(view.focused()==1 && cues.back()==a::SystemCue::focus);view.update(pointer(473,377));CHECK(view.focused()==0);view.update(pointer(473,377,true));CHECK(view.take_answer()==true && !view.take_answer() && view.phase()==g::ConfirmationPhase::closing);++cases;
    const auto yes=std::find_if(view.sprites().begin(),view.sprites().end(),[](const auto& sprite){return sprite.order==0x534;});CHECK(yes!=view.sprites().end() && yes->source_y==80);view.update(pointer(649,377,true));CHECK(!view.take_answer());view.update();CHECK(view.phase()==g::ConfirmationPhase::closed && view.sprites().empty());++cases;
    view.prepare(g::options_reset_prompt(),1,0,0);until([&]{return view.phase()==g::ConfirmationPhase::active;},[&]{view.update();});view.update({{false,false,false,0,0,-1},false});CHECK(view.focused()==1);view.update({{false,false,true},false});CHECK(view.take_answer()==false && cues.back()==a::SystemCue::cancel);view.update();CHECK(view.phase()==g::ConfirmationPhase::closed);++cases;
    view.prepare(g::options_reset_prompt());view.update({{},true});CHECK(view.take_answer()==false);until([&]{return view.phase()==g::ConfirmationPhase::closed;},[&]{view.update();});++cases;
    view.prepare(g::options_reset_prompt(),0,0,0);until([&]{return view.phase()==g::ConfirmationPhase::active;},[&]{view.update();});const auto owner=view.sprites()[0].image;const auto previous=view.phase();text_failure=true;rejects([&]{view.prepare(g::options_reset_prompt());});++guards;text_failure=false;missing=true;rejects([&]{view.prepare(g::options_reset_prompt());});++guards;missing=false;CHECK(view.phase()==previous && view.sprites()[0].image==owner);++cases;
    auto invalid=pointer(std::numeric_limits<float>::infinity(),0);rejects([&]{view.update(invalid);});++guards;invalid={};invalid.selection.navigation=2;rejects([&]{view.update(invalid);});++guards;
    {i::ImageStore small(r::Archive::open(bank({{"sys_00100.tga",raster(1,1)}})));g::ConfirmationView bad([&](auto name){return small.request(name);},text);bad.prepare(g::options_reset_prompt());rejects([&]{until([&]{return bad.phase()==g::ConfirmationPhase::active;},[&]{bad.update();});});++guards;}++cases;
    g::Preferences modified;modified.fullscreen=true;modified.captions=false;modified.skin=1;modified.master=0;modified.speech=12;modified.window=false;modified.character_voice.fill(false);modified.audio_muted.fill(true);modified.voice_enabled=false;modified.right_click=2;
    const auto defaults=g::options_defaults(modified);CHECK(defaults.fullscreen && !defaults.captions && defaults.skin==0 && defaults.speech==256 && defaults.master==195 && defaults.window && defaults.character_voice[9] && defaults.voice_enabled && defaults.right_click==0 && !defaults.audio_muted[2]);++cases;
    std::vector<std::shared_ptr<VoiceState>> effects,speech;unsigned issued{};bool skip_speech{};
    const s::SceneEffects::Factory effect_factory=[&](int id,bool loop)->std::unique_ptr<a::Voice>{auto state=std::make_shared<VoiceState>();state->id=id;state->loop=loop;effects.push_back(state);return std::make_unique<Voice>(state);};
    const s::SceneVoices::Factory voice_factory=[&](const auto& line)->std::unique_ptr<a::Voice>{auto state=std::make_shared<VoiceState>();state->line=line;speech.push_back(state);return std::make_unique<Voice>(state);};
    s::SceneEffects channels(effect_factory);s::SceneVoices voices(voice_factory,[]{return 1001;},[]{return 0;},[]{return 987;},[&]{return skip_speech;},{},{ {},{},[&](auto,const auto&,auto){++issued;} });
    g::Preferences prefs;channels.settings(prefs.master,prefs.effects);voices.settings(prefs.master,prefs.speech);wa2::Random random(1),expected(1);
    channels.play(7,77,0,1,255);g::options_preview_effects(channels,prefs);CHECK(channels.slot(15).sound==8101 && channels.slot(7).sound==77 && !effects.back()->loop && !effects[0]->stops);g::options_preview_effects(channels,prefs);CHECK(effects[1]->stops==1 && !effects[2]->stops && !effects[0]->stops);++cases;
    const auto effect_count=effects.size();prefs.audio_muted[2]=true;g::options_preview_effects(channels,prefs);CHECK(effects.size()==effect_count);prefs.audio_muted[2]=false;g::options_reset_sound(channels);CHECK(effects.back()->id==8905 && !effects.back()->loop);++cases;
    constexpr std::array<s::SpokenLine,5> samples{{{1,2503,99,false},{2,2002,242,false},{3,2027,223,false},{4,2001,86,false},{5,2001,198,false}}};
    for(unsigned n=0;n<20;++n){const auto line=samples[expected.next()%5];g::options_preview_voice(voices,random,prefs);CHECK(speech.back()->line.speaker==line.speaker && speech.back()->line.chapter==line.chapter && speech.back()->line.line==line.line+987 && !speech.back()->line.loop && !issued);if(n)CHECK(speech[n-1]->stops==1);}
    CHECK(random.state()==expected.state() && voices.active()==1 && !speech.back()->stops);++cases;
    skip_speech=true;const auto skipped=speech.size();g::options_preview_voice(voices,random,prefs);CHECK(speech.size()==skipped && !issued);skip_speech=false;++cases;
    voices.settings(256,256);CHECK(speech.back()->gain==255);voices.settings(0,256);CHECK(speech.back()->gain==0 && !speech.back()->stops);voices.enabled(false);const auto voice_count=speech.size();g::options_preview_voice(voices,random,prefs);CHECK(speech.size()==voice_count);voices.enabled(true);voices.character_switches({});g::options_preview_voice(voices,random,prefs);CHECK(speech.size()==voice_count);++cases;
    auto banks=memory_banks();
    std::map<std::string,Bytes> title_images;const auto old=r::Archive::open(banks.at("grp.pak"));for(unsigned n=0;n<old.members().size();++n)title_images[old.members()[n].name]=old.load(n);
    constexpr std::string_view title_names[]{"T0000.tga","T0100.tga","T0010.tga","T0020.tga","T0050.tga","T0101.tga","T0200.tga","T0201.tga","T0011.tga"};constexpr unsigned widths[]{1280,640,752,264,408,320,288,288,752},heights[]{856,640,448,24,24,80,320,160,448};
    for(unsigned n=0;n<9;++n)title_images[std::string(title_names[n])]=raster(widths[n],heights[n]);banks["grp.pak"]=bank(std::move(title_images));
    const r::BankOpener opener=[&](auto name)->std::shared_ptr<r::Source>{const auto it=banks.find(std::string(name));return it==banks.end()?nullptr:it->second;};
    auto settings=std::make_shared<Settings>();auto progress=std::make_shared<p::Store>(std::make_shared<Storage>());g::SceneServices services;services.effects=[&](int id,bool loop,bool){return effect_factory(id,loop);};services.speech=voice_factory;
    g::Application app(opener,progress,settings,std::make_shared<Saves>(),memory_resolver(),1,services);app.prepare_options(loader,{},text);app.start({},0);std::int64_t now{};
    const auto tick=[&](g::Input input={}){now+=16667;app.frame(now,input,[&]{return now;});};
    until([&]{return app.scene()->statistics().command==194 && app.scene()->background().current;},[&]{tick();});const auto ticks=app.scene()->statistics().ticks;const auto mixer=app.mixer();
    auto changed=app.preferences();changed.window_alpha=256;changed.music=2;changed.speech=3;app.preferences(changed);app.open(g::Page::options,now);
    const auto ready=[&]{until([&]{return app.options_view()->phase()==g::OptionsPhase::active;},[&]{tick();});};ready();
    const auto click=[&](unsigned id){auto found=std::find_if(app.options_view()->hits().begin(),app.options_view()->hits().end(),[&](const auto& hit){return hit.id==id;});CHECK(found!=app.options_view()->hits().end());g::Input input;input.selection={true,true,false,found->x+1,found->y+1};tick(input);};
    const auto answer=[&](bool yes){g::Input input;input.selection={true,true,false,float(yes?473:649),377};tick(input);};
    click(3);CHECK(app.options_view()->confirmation() && app.preferences()==changed);until([&]{return app.options_view()->confirmation()->phase()==g::ConfirmationPhase::active;},[&]{tick();});
    g::RenderFrame frame;frame.capture(app);CHECK(std::any_of(frame.overlays().begin(),frame.overlays().end(),[](const auto& sprite){return sprite.order==0x534;}));
    const auto writes=settings->writes;answer(false);until([&]{return !app.options_view()->confirmation();},[&]{tick();});CHECK(app.preferences()==changed && writes==settings->writes);++cases;
    click(3);until([&]{return app.options_view()->confirmation()->phase()==g::ConfirmationPhase::active;},[&]{tick();});answer(true);CHECK(app.preferences()==g::options_defaults(changed) && app.scene()->preferences()==app.preferences() && settings->writes==writes+1 && effects.back()->id==8905);until([&]{return !app.options_view()->confirmation();},[&]{tick();});CHECK(app.mixer()==mixer && app.scene()->statistics().ticks==ticks);++cases;
    changed=app.preferences();changed.confirmation=false;changed.speech=1;app.preferences(changed);settings->fail=true;const auto before=settings->writes;rejects([&]{click(3);});++guards;CHECK(app.preferences()==changed && settings->writes==before && !app.options_view()->confirmation());settings->fail=false;click(3);CHECK(app.preferences()==g::options_defaults(changed) && !app.options_view()->confirmation());++cases;
    click(1);ready();const auto sound_count=effects.size();click(55);CHECK(effects.size()==sound_count+1 && effects.back()->id==8101 && !effects.back()->loop);const auto spoken=speech.size();click(65);CHECK(speech.size()==spoken+1 && !speech.back()->line.loop);const auto sample=speech.back();click(21);CHECK(app.preferences().right_click==1 && g::PreferenceStore(settings).current().right_click==1);app.back(now);CHECK(!sample->stops && app.mixer()==mixer);++cases;
    tick();g::Input right;right.toggle_window=true;const auto before_menu=app.scene()->statistics().ticks;const auto before_story=g::SavedScene::decode(app.scene()->checkpoint());tick(right);CHECK(app.page()==g::Page::options && app.scene()->statistics().ticks==before_menu && app.mixer()==mixer);
    auto menu_story=g::SavedScene::decode(app.scene()->checkpoint());CHECK(menu_story.logical_us==before_story.logical_us+16667);menu_story.logical_us=before_story.logical_us;CHECK(menu_story.encode()==before_story.encode());ready();++cases;
    tick(right);CHECK(app.options_view()->phase()==g::OptionsPhase::closing);until([&]{return app.page()==g::Page::story;},[&]{tick();});CHECK(app.scene()->statistics().ticks==before_menu && app.mixer()==mixer);tick();++cases;
    auto unsupported=app.preferences();unsupported.right_click=2;app.preferences(unsupported);const auto before_unsupported=app.scene()->checkpoint();rejects([&]{tick(right);});++guards;CHECK(app.page()==g::Page::story && app.scene()->checkpoint()==before_unsupported && !app.options_view());unsupported.right_click=1;app.preferences(unsupported);++cases;
    {g::Application unprepared(opener,progress,settings,std::make_shared<Saves>(),memory_resolver(),1,services);unprepared.start({},now);unprepared.frame(now,{},[&]{return now;});rejects([&]{unprepared.frame(now+1,right,[&]{return now+1;});});++guards;CHECK(unprepared.page()==g::Page::story && !unprepared.options_view());unprepared.close(now+2);}++cases;
    app.close(now);CHECK(sample->stops && effects[sound_count]->stops);++cases;
    g::TitleServices title_services;title_services.music=effect_factory;title_services.effects=effect_factory;title_services.speech=voice_factory;
    {g::TitleScene title(opener,progress,prefs,{}, {false,0},7,title_services);title.options_preview_effects();CHECK(effects.back()->id==8101);title.options_preview_voice();CHECK(!speech.back()->line.loop);title.options_reset_sound();CHECK(effects.back()->id==8905);title.pause(true,0);CHECK(title.mixer()->paused());title.pause(false,1);CHECK(!title.mixer()->paused());}CHECK(speech.back()->stops && effects.back()->stops);++cases;
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.native-options-actions.v1\",\"cases\":"<<cases<<",\"guards\":"<<guards<<",\"source_modal_geometry\":true,\"confirmation_gates_reset\":true,\"source_defaults_preserved\":true,\"previews_replace_bounded_channels\":true,\"no_preview_story_history\":true,\"same_mixer_from_title_and_story\":true,\"right_click_settings_route\":true,\"deferred_font_geometry_only\":true,\"synthetic_resources_only\":true,\"original_resources_opened\":false,\"devices_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";CHECK(out);std::cout<<"PASS native options confirmation, defaults and shared bounded audio previews.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
