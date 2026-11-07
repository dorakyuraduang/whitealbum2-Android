#define SDL_MAIN_HANDLED
#include "sdl_application.hpp"
#include "native_scene_fixture.hpp"
#include <SDL.h>

namespace platform=wa2::platform;
struct Settings:g::PreferenceStorage {std::optional<Bytes> value;std::optional<Bytes> read()override{return value;}void replace(std::span<const std::uint8_t> bytes)override{value=Bytes(bytes.begin(),bytes.end());}};
struct Saves:g::SaveStorage {std::optional<Bytes> read(unsigned)override{return {};}void replace(unsigned,std::span<const std::uint8_t>)override{}std::vector<g::SaveSlotInfo> list()override{return {};}};
struct OutputState {std::weak_ptr<wa2::audio::Mixer> owner;unsigned opens{},closes{},pauses{},destroyed{};bool paused{},bad_owner{};};
struct Output:platform::AudioOutput {
    std::shared_ptr<OutputState> state;explicit Output(std::shared_ptr<OutputState> value):state(std::move(value)){}~Output(){++state->destroyed;state->bad_owner|=state->owner.expired();}
    bool open(const std::string&)override{++state->opens;return true;}void close()override{++state->closes;state->bad_owner|=state->owner.expired();}void pause(bool value)override{++state->pauses;state->paused=value;}std::vector<std::string> devices()override{return {};}platform::AudioStatus status()const override{return {};}
};
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{},guards{};SDL_SetMainReady();CHECK(SDL_Init(0)==0);
    std::unique_ptr<SDL_Surface,decltype(&SDL_FreeSurface)> surface(SDL_CreateRGBSurfaceWithFormat(0,64,36,32,SDL_PIXELFORMAT_RGBA32),SDL_FreeSurface);CHECK(surface);
    {
        auto* renderer=SDL_CreateSoftwareRenderer(surface.get());CHECK(renderer);auto canvas=platform::SdlCanvas::adopt(renderer);canvas->logical_size(1280,720);
        const auto window_services=canvas->window_services();CHECK(!window_services.fullscreen && !window_services.error);++cases;
        auto banks=memory_banks();Script script;script.call(176,{s::Value(0),s::Value(std::string("atlas.tga")),s::Value(24),s::Value(1),s::Value(std::string()),s::Value(256),s::Value(std::string("grp")),s::Value(1)});script.call(194,{s::Value(1000000000),s::Value(0),s::Value(1)});script.finish();
        banks["ck-gal.pak"]=bank({{"Global.vrb",text("3,FLG_00,0\r\n")},{"LFScriptFunc.fnc",functions(0,200)},{"LFScriptFuncEx.fnc",functions(200,239)},{"1001.bnr",script.code},{"1001.txt",text(script.strings)}});
        g::Application app([&](auto name)->std::shared_ptr<r::Source>{const auto found=banks.find(std::string(name));return found==banks.end()?nullptr:found->second;},std::make_shared<p::Store>(std::make_shared<Storage>()),std::make_shared<Settings>(),std::make_shared<Saves>(),memory_resolver());app.start({},0);
        std::vector<std::shared_ptr<OutputState>> outputs;unsigned submitted{};platform::SdlApplication runner(app,*canvas,{},[&](auto& mixer)->std::unique_ptr<platform::AudioOutput>{CHECK(app.mixer().get()==&mixer);auto state=std::make_shared<OutputState>();state->owner=app.mixer();outputs.push_back(state);return std::make_unique<Output>(state);},[&]{++submitted;});
        platform::InputSnapshot events;auto frame=runner.pump(0,events,[]{return 0;});CHECK(frame.updates==1 && frame.draw && submitted==1 && outputs.size()==1 && outputs[0]->opens==1);++cases;
        frame=runner.pump(0,events,[]{return 0;});CHECK(!frame.updates && !frame.draw && submitted==1 && outputs[0]->opens==1);++cases;
        const auto initial_ticks=app.scene()->statistics().ticks;frame=runner.pump(8334,events,[]{return 0;});CHECK(!frame.updates && frame.draw && submitted==2 && app.scene()->statistics().ticks==initial_ticks);++cases;
        frame=runner.pump(16667,events,[]{return 0;});CHECK(frame.updates==1 && frame.draw && submitted==3 && app.scene()->statistics().ticks==initial_ticks+1);++cases;
        std::int64_t now=16667;const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);while(app.scene()->foreground().empty()){CHECK(std::chrono::steady_clock::now()<deadline);now+=16667;runner.pump(now,events,[]{return 0;});std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        now+=16667;runner.pump(now,events,[]{return 0;});CHECK(canvas->statistics().uploads>0 && submitted==canvas->statistics().presents);++cases;
        g::RenderFrame retained_frame;retained_frame.capture(app);const auto save_ticks=app.scene()->statistics().ticks;const auto save_presents=canvas->statistics().presents;
        const auto thumbnail=platform::save_thumbnail(retained_frame);CHECK(thumbnail && thumbnail->width==128 && thumbnail->height==72 && thumbnail->rgba.size()==128u*72*4 && app.scene()->statistics().ticks==save_ticks && canvas->statistics().presents==save_presents);++cases;
        i::Sprite fill;fill.image=std::make_shared<const i::Raster>(i::Raster{1,1,{191,57,31,255}});fill.width=1280;fill.height=720;fill.source_width=fill.source_height=1;
        const std::array fills{fill};const std::array<std::span<const i::Sprite>,1> groups{fills};g::RenderFrame solid;solid.compose({}, {},groups);const auto solid_thumbnail=platform::save_thumbnail(solid);
        for(std::size_t at=0;at<solid_thumbnail->rgba.size();at+=4)CHECK(solid_thumbnail->rgba[at]==191 && solid_thumbnail->rgba[at+1]==57 && solid_thumbnail->rgba[at+2]==31 && solid_thumbnail->rgba[at+3]==255);++cases;
        const auto uploads=canvas->statistics().uploads;now+=16667;events.renderer_reset=true;runner.pump(now,events,[]{return 0;});events.renderer_reset=false;CHECK(canvas->statistics().uploads>uploads && outputs[0]->opens==1 && submitted==canvas->statistics().presents);++cases;
        auto old=std::weak_ptr(app.mixer());app.start({},now);runner.pump(now,events,[]{return 0;});CHECK(outputs.size()==2 && outputs[0]->closes==1 && outputs[0]->destroyed==1 && !outputs[0]->bad_owner && old.expired() && outputs[1]->opens==1);++cases;
        const auto ticks=app.scene()->statistics().ticks,draws=canvas->statistics().presents;events.focused=false;now+=1;frame=runner.pump(now,events,[]{return 0;});CHECK(frame.suspended && !frame.draw && !frame.updates && outputs[1]->paused && app.mixer()->paused());++cases;
        now+=50000000;runner.pump(now,events,[]{return 0;});CHECK(app.scene()->statistics().ticks==ticks && canvas->statistics().presents==draws);++cases;
        events.focused=true;now+=1;frame=runner.pump(now,events,[]{return 0;});CHECK(frame.updates==1 && frame.draw && !frame.dropped_updates && !outputs[1]->paused && !app.mixer()->paused());++cases;
        platform::SdlInput input(9);input.logical_size(1280,720);SDL_Event event{};event.type=SDL_MOUSEBUTTONDOWN;event.button.windowID=9;event.button.button=SDL_BUTTON_LEFT;event.button.x=700;event.button.y=400;input.consume(event);
        auto game=platform::game_input(input.snapshot());CHECK(game.advance && game.pointer_held && game.selection.pointer && game.selection.activate && game.selection.x==700 && game.selection.y==400);++cases;
        input.begin_frame();game=platform::game_input(input.snapshot());CHECK(!game.advance && !game.selection.activate && game.selection.pointer && game.pointer_held);++cases;
        event={};event.type=SDL_KEYDOWN;event.key.windowID=9;event.key.keysym.scancode=SDL_SCANCODE_LCTRL;input.consume(event);CHECK(platform::game_input(input.snapshot()).skip);event.key.keysym.scancode=SDL_SCANCODE_DOWN;input.consume(event);CHECK(platform::game_input(input.snapshot()).selection.navigation==1);++cases;
        event.key.keysym.scancode=SDL_SCANCODE_RETURN;input.consume(event);game=platform::game_input(input.snapshot());CHECK(game.advance && game.selection.confirm);++cases;
        event.key.keysym.scancode=SDL_SCANCODE_PAGEDOWN;input.consume(event);CHECK(platform::game_input(input.snapshot()).page_delta==1);input.begin_frame();CHECK(!platform::game_input(input.snapshot()).page_delta);++cases;
        event={};event.type=SDL_WINDOWEVENT;event.window.windowID=9;event.window.event=SDL_WINDOWEVENT_FOCUS_LOST;input.consume(event);game=platform::game_input(input.snapshot());CHECK(!game.skip && !game.selection.pointer && !game.pointer_held && !input.snapshot().buttons.any());++cases;
        rejects([&]{runner.run(input,{},{});});rejects([&]{runner.run(input,[]{return 0;},{});});guards+=2;++cases;
        const auto presents=canvas->statistics().presents;events.quit=true;now+=1;frame=runner.pump(now,events,[]{return 0;});CHECK(frame.closed && !frame.draw && app.page()==g::Page::closed && !app.mixer() && outputs[1]->closes==1 && outputs[1]->destroyed==1 && !outputs[1]->bad_owner && canvas->statistics().presents==presents);++cases;
    }
    surface.reset();SDL_Quit();std::ofstream report(argv[1]);report<<"{\"schema\":\"wa2.native-sdl-application.v1\",\"cases\":"<<cases<<",\"guards\":"<<guards<<",\"owned_memory_only\":true,\"memory_renderer_has_no_window_services\":true,\"surface_size\":[64,36],\"fake_audio_outputs_only\":true,\"synthetic_event_structs_only\":true,\"actual_present_hook\":true,\"os_event_queue_polled\":false,\"original_resources_opened\":false,\"windows_created\":0,\"devices_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";CHECK(report);std::cout<<"PASS common SDL application pump with owned-memory frames, event structs and fake output handoffs.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
