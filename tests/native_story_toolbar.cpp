#include "application.hpp"
#include "render_frame.hpp"
#include "native_scene_fixture.hpp"
#include <algorithm>

struct Settings:g::PreferenceStorage {Bytes bytes=g::Preferences{}.encode();std::optional<Bytes> read()override{return bytes;}void replace(std::span<const std::uint8_t> value)override{bytes.assign(value.begin(),value.end());}};
struct Saves:g::SaveStorage {unsigned writes{};std::optional<Bytes> read(unsigned)override{return {};}void replace(unsigned,std::span<const std::uint8_t>)override{++writes;}std::vector<g::SaveSlotInfo> list()override{return {};}};
template<class F>void until(F&& action){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(4);unsigned count{};while(!action()){CHECK(++count<1000 && std::chrono::steady_clock::now()<end);std::this_thread::sleep_for(std::chrono::milliseconds(1));}}
std::optional<i::Sprite> find(std::span<const i::Sprite> sprites,int order){const auto it=std::find_if(sprites.begin(),sprites.end(),[&](const auto& item){return item.order==order;});return it==sprites.end()?std::nullopt:std::optional(*it);}
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{},guards{};auto banks=memory_banks();i::ImageStore images(r::Archive::open(banks.at("grp.pak")));wa2::text::FontBook fonts(r::Archive{});
    const auto loader=[&](auto name){return images.request(name);};s::SceneDialogue dialogue(fonts,loader,{},[]{});
    std::array<s::Value,3> message{s::Value(std::string()),s::Value(7),s::Value(1)};s::Command output{133,message};CHECK(dialogue.dispatch(output)->complete);until([&]{dialogue.update(true);return dialogue.ready();});
    auto settings=dialogue.settings();settings.button_alpha=80;dialogue.settings(settings);const auto saved=dialogue.checkpoint();
    auto sprites=dialogue.sprites();CHECK(find(sprites,0x40f)->alpha==80 && find(sprites,0x418)->alpha==80 && !find(sprites,0x419));++cases;
    s::StoryToolbar pointer;pointer.hover=s::StoryControl::options;pointer.near_bottom=true;
    for(unsigned step=1;step<=16;++step){dialogue.toolbar(pointer);sprites=dialogue.sprites();const auto alpha=std::min(255u,(16-step)*80/16+step*16);CHECK(find(sprites,0x40f)->alpha==alpha && find(sprites,0x418)->alpha==alpha && find(sprites,0x411)->source_y==40 && find(sprites,0x40f)->source_y==0);
        const auto tooltip=find(sprites,0x419);CHECK(tooltip && tooltip->layer==0x104e && tooltip->space==i::SpriteSpace::screen && tooltip->source_y==32 && tooltip->width==208 && tooltip->height==16 && tooltip->y==664);if(step==1)CHECK(tooltip->x==944 && tooltip->alpha==16);}
    CHECK(dialogue.checkpoint()==saved);++cases;dialogue.toolbar(pointer);const auto tooltip=find(dialogue.sprites(),0x419);CHECK(tooltip->alpha==255 && tooltip->x==1000);++cases;
    pointer.hover=s::StoryControl::skip;pointer.pressed=true;dialogue.toolbar(pointer);CHECK(find(dialogue.sprites(),0x415)->source_y==80 && find(dialogue.sprites(),0x419)->source_y==96);++cases;
    pointer.pressed=false;pointer.skipping=true;dialogue.toolbar(pointer);CHECK(find(dialogue.sprites(),0x415)->source_y==40 && find(dialogue.sprites(),0x419)->source_y==112);++cases;
    pointer.hover=s::StoryControl::automatic;dialogue.toolbar(pointer);CHECK(find(dialogue.sprites(),0x419)->source_y==128);pointer.automatic=true;dialogue.toolbar(pointer);CHECK(find(dialogue.sprites(),0x419)->source_y==144);++cases;
    pointer.hover=s::StoryControl::hide;dialogue.toolbar(pointer);CHECK(find(dialogue.sprites(),0x419)->source_y==160);++cases;
    pointer.hover.reset();pointer.near_bottom=false;for(unsigned n=0;n<16;++n)dialogue.toolbar(pointer);sprites=dialogue.sprites();CHECK(!find(sprites,0x419) && find(sprites,0x40f)->alpha==80 && find(sprites,0x418)->alpha==80);++cases;
    pointer.hover=s::StoryControl::save;pointer.replay=true;dialogue.toolbar(pointer);sprites=dialogue.sprites();for(unsigned n=0;n<9;++n){const auto button=find(sprites,0x40f+int(n));const auto dim=n==0 || n==1 || n==3 || n==4;CHECK(button && button->red==(dim?128:255) && button->green==button->red && button->blue==button->red);}CHECK(find(sprites,0x412)->source_y==40);++cases;
    pointer.demo=true;dialogue.toolbar(pointer);sprites=dialogue.sprites();for(unsigned n=0;n<9;++n)CHECK(find(sprites,0x40f+int(n)).has_value()==(n==7));CHECK(!find(sprites,0x418) && !find(sprites,0x419));++cases;
    pointer.hover=s::StoryControl::automatic;dialogue.toolbar(pointer);CHECK(find(dialogue.sprites(),0x416)->source_y==40 && find(dialogue.sprites(),0x419)->source_y==144);++cases;
    auto invalid=pointer;invalid.hover=s::StoryControl(99);rejects([&]{dialogue.toolbar(invalid);});++guards;
    dialogue.restore(saved);until([&]{dialogue.update(true);return dialogue.ready();});sprites=dialogue.sprites();CHECK(!find(sprites,0x419) && find(sprites,0x40f)->alpha==80 && !fonts.source_bytes());++cases;
    dialogue.visible(false);dialogue.toolbar(pointer);CHECK(dialogue.sprites().empty());dialogue.visible(true);CHECK(!find(dialogue.sprites(),0x419));++cases;
    // Actual common application frames, with constructed empty-message/wait
    // scripts. Their images are blank fixtures; no original pixels are used.
    Script story,demo;story.call(133,{s::Value(std::string()),s::Value(7),s::Value(1)});story.call(194,{s::Value(100000000),s::Value(0),s::Value(1)});story.finish();
    demo.call(136,{s::Value(1),s::Value(600)});demo.call(133,{s::Value(std::string()),s::Value(7),s::Value(1)});demo.call(194,{s::Value(100000000),s::Value(0),s::Value(1)});demo.finish();
    banks["ck-gal.pak"]=bank({{"Global.vrb",text("3,FLG_00,0\r\n")},{"LFScriptFunc.fnc",functions(0,200)},{"LFScriptFuncEx.fnc",functions(200,239)},{"1001.bnr",story.code},{"1001.txt",text(story.strings)},{"2001.bnr",demo.code},{"2001.txt",text(demo.strings)}});
    auto storage=std::make_shared<Saves>();auto prefs=std::make_shared<Settings>();g::Application app([&](auto name)->std::shared_ptr<r::Source>{const auto found=banks.find(std::string(name));return found==banks.end()?nullptr:found->second;},std::make_shared<p::Store>(std::make_shared<Storage>()),prefs,storage,memory_resolver());
    auto preferences=app.preferences();preferences.button_alpha=80;preferences.skip_read_only=false;preferences.transition_wait=false;app.preferences(preferences);std::int64_t now{};
    const auto frame=[&](g::Input input={},g::FrameBudget budget={0,1}){now+=16667;return app.frame(now,input,[]{return 0;},budget);};
    app.start({},now);until([&]{frame();return app.scene()->dialogue_state().ready() && app.scene()->statistics().command==194;});
    app.scene()->story_control(s::StoryControl::skip);g::Input hover;hover.selection={true,false,false,1089,681,0};const auto batch=frame(hover,{2000,64});CHECK(batch.ticks==64);g::RenderFrame rendered;rendered.capture(app);CHECK(find(rendered.overlays(),0x415)->alpha==91 && find(rendered.overlays(),0x415)->source_y==40 && find(rendered.overlays(),0x419)->alpha==16);++cases;
    const auto checkpoint=app.scene()->checkpoint();rendered.capture(app);rendered.capture(app);CHECK(app.scene()->checkpoint()==checkpoint && find(rendered.overlays(),0x415)->alpha==91 && find(rendered.overlays(),0x419)->alpha==16);++cases;
    app.background(true,now);for(unsigned n=0;n<20;++n)frame(hover);CHECK(find(app.scene()->dialogue(),0x415)->alpha==91 && app.scene()->checkpoint()==checkpoint);app.background(false,now);app.scene()->story_control(s::StoryControl::skip);frame();++cases;
    app.scene()->story_control(s::StoryControl::automatic);CHECK(app.scene()->modes().auto_mode);g::Input advance;advance.advance=true;frame(advance);CHECK(!app.scene()->modes().auto_mode);++cases;
    app.scene()->story_control(s::StoryControl::automatic);CHECK(app.scene()->modes().auto_mode);g::Input hide;hide.toggle_window=true;frame(hide);CHECK(!app.scene()->modes().auto_mode && !app.scene()->window_visible());app.scene()->story_control(s::StoryControl::hide);++cases;
    app.prepare_options(loader);g::SaveServices saves;saves.images=loader;saves.text=[](auto raw,const auto& style){return wa2::text::FontBook::defer(wa2::text::layout(raw,style));};saves.stamp=[] {return g::SaveStamp{};};saves.thumbnail=[](auto&)->std::shared_ptr<const i::Raster>{return std::make_shared<i::Raster>(i::Raster{128,72,Bytes(128*72*4)});};app.prepare_saves(std::move(saves));
    const auto available=app.user_menu().enabled;for(auto command:{g::UserCommand::settings,g::UserCommand::save,g::UserCommand::load,g::UserCommand::quick_save,g::UserCommand::quick_load})CHECK(available&g::command_bit(command));++cases;
    app.scene()->replay_mode(1);for(auto command:{g::UserCommand::save,g::UserCommand::load,g::UserCommand::quick_save,g::UserCommand::quick_load})CHECK(!(app.user_menu().enabled&g::command_bit(command)));CHECK(app.user_menu().enabled&g::command_bit(g::UserCommand::settings));++cases;
    const auto replay=app.scene()->checkpoint();g::Input click;click.advance=true;click.selection={true,true,false,969,681,0};CHECK(!frame(click).ticks && app.page()==g::Page::story && app.scene()->checkpoint()==replay && !storage->writes);++cases;
    CHECK(!app.user_command(g::UserCommand::save,now) && app.scene()->checkpoint()==replay && !storage->writes);++cases;
    app.scene()->story_control(s::StoryControl::automatic);CHECK(app.scene()->modes().auto_mode);frame(click);CHECK(!app.scene()->modes().auto_mode && app.page()==g::Page::story && !storage->writes);++cases;
    app.start({"2001",0,0},now);until([&]{frame();return app.scene()->dialogue_state().ready() && app.scene()->statistics().command==194;});CHECK(app.scene()->modes().demo_mode==1);for(unsigned n=0;n<9;++n)CHECK(app.scene()->control_allowed(s::StoryControl(n))==(n==7));++cases;
    for(auto command:{g::UserCommand::settings,g::UserCommand::save,g::UserCommand::load,g::UserCommand::quick_save,g::UserCommand::quick_load,g::UserCommand::hide,g::UserCommand::skip})CHECK(!(app.user_menu().enabled&g::command_bit(command)));CHECK(app.user_menu().enabled&g::command_bit(g::UserCommand::automatic));++cases;
    CHECK(!app.scene()->control_at(969,681) && app.scene()->control_at(1129,681)==s::StoryControl::automatic && !app.user_command(g::UserCommand::hide,now) && app.scene()->window_visible());++cases;
    rejects([&]{app.scene()->control_allowed(s::StoryControl(99));});++guards;
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.native-story-toolbar.v1\",\"cases\":"<<cases<<",\"guards\":"<<guards<<",\"source_hover_tooltips\":true,\"source_bottom_alpha_ramp\":true,\"source_demo_replay_policy\":true,\"disabled_click_does_not_advance\":true,\"skip_does_not_accelerate_toolbar\":true,\"capture_does_not_tick\":true,\"suspension_freezes_toolbar\":true,\"manual_click_stops_auto\":true,\"transient_hover_not_saved\":true,\"synthetic_resources_only\":true,\"geometry_only\":true,\"original_resources_opened\":false,\"devices_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";CHECK(out);std::cout<<"PASS native story toolbar, shared permissions and unaccelerated pointer animation.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
