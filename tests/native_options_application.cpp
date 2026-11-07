#include "application.hpp"
#include "render_frame.hpp"
#include "native_scene_fixture.hpp"
#include "native_options_atlases.hpp"
#include <algorithm>

struct Settings:g::PreferenceStorage {
    std::optional<Bytes> bytes;unsigned writes{};bool fail{};
    std::optional<Bytes> read()override{return bytes;}
    void replace(std::span<const std::uint8_t> data)override{if(fail)throw std::runtime_error("injected preference write failure");bytes=Bytes(data.begin(),data.end());++writes;}
};
struct Saves:g::SaveStorage {
    std::optional<Bytes> read(unsigned)override{return {};}
    void replace(unsigned,std::span<const std::uint8_t>)override{}
    std::vector<g::SaveSlotInfo> list()override{return {};}
};
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{},guards{},image_requests{};bool missing{};
    auto banks=memory_banks();const r::BankOpener opener=[&](auto name)->std::shared_ptr<r::Source>{const auto found=banks.find(std::string(name));return found==banks.end()?nullptr:found->second;};
    std::map<std::string,Bytes> menu_images;for(const auto& atlas:wa2::test::options_atlases)menu_images[atlas.name]=raster(atlas.w,atlas.h);
    i::ImageStore images(r::Archive::open(bank(std::move(menu_images))));
    auto settings=std::make_shared<Settings>();auto progress=std::make_shared<p::Store>(std::make_shared<Storage>());auto saves=std::make_shared<Saves>();
    g::Application app(opener,progress,settings,saves,memory_resolver());std::vector<g::OptionsCommand::Kind> commands;
    rejects([&]{app.prepare_options({});});++guards;
    const g::OptionsView::Loader load=[&](auto name){++image_requests;if(missing)return std::shared_ptr<i::ImageRequest>{};return images.request(name);};
    app.prepare_options(load,[&](auto command){commands.push_back(command.kind);});CHECK(!app.options_view() && !image_requests && !settings->writes);++cases;
    rejects([&]{app.open(g::Page::options,0);});CHECK(app.page()==g::Page::title && !app.options_view() && !image_requests);++guards;++cases;
    app.start({},0);std::int64_t now{};const auto tick=[&](g::Input input={}){now+=16667;return app.frame(now,input,[&]{return now;});};
    const auto until=[&](auto predicate){const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);unsigned ticks{};while(!predicate()){CHECK(++ticks<1200 && std::chrono::steady_clock::now()<deadline);tick();std::this_thread::sleep_for(std::chrono::milliseconds(1));}};
    until([&]{return app.scene()->statistics().command==194 && app.scene()->background().current;});auto mixer=app.mixer();auto* scene=app.scene();const auto frozen=scene->checkpoint();const auto frozen_ticks=scene->statistics().ticks;
    app.open(g::Page::options,now);CHECK(app.options_view() && app.options_view()->phase()==g::OptionsPhase::loading && app.page()==g::Page::options && image_requests==1 && app.mixer()==mixer);until([&]{return app.options_view()->phase()==g::OptionsPhase::active;});CHECK(scene->checkpoint()==frozen && scene->statistics().ticks==frozen_ticks && !mixer->paused() && !settings->writes);++cases;
    rejects([&]{app.prepare_options(load);});++guards;
    CHECK(!app.options_view()->context().title && app.options_view()->context().theme==scene->theme());CHECK(std::any_of(app.options_view()->sprites().begin(),app.options_view()->sprites().end(),[](const auto& sprite){return sprite.order==0x4a7 && sprite.x==40 && sprite.y==16 && sprite.source_y==16;}));++cases;
    g::RenderFrame frame;frame.capture(app);CHECK(frame.background().current && frame.overlays().size()>=app.options_view()->sprites().size() && !frame.movie_active());const auto panel=std::find_if(frame.overlays().begin(),frame.overlays().end(),[](const auto& sprite){return sprite.order==0x4b5;});CHECK(panel!=frame.overlays().end() && panel->x==128 && panel->y==112);auto retained=frame;std::weak_ptr<const i::Raster> owner=panel->image;CHECK(!owner.expired());++cases;
    const auto click=[&](unsigned id,float offset=1.f,bool held=false){const auto hits=app.options_view()->hits();const auto found=std::find_if(hits.begin(),hits.end(),[&](const auto& hit){return hit.id==id;});CHECK(found!=hits.end());g::Input input;input.pointer_held=held;input.selection={true,true,false,found->x+offset,found->y+1,0};return tick(input);};
    click(62,335,true);CHECK(app.preferences().window_alpha==256 && scene->dialogue_state().settings().window_alpha==256 && g::Preferences::decode(*settings->bytes).window_alpha==256 && settings->writes==1);++cases;
    g::Input drag;drag.pointer_held=true;drag.selection={true,false,false,-500,-500,0};tick(drag);CHECK(app.preferences().window_alpha==0 && app.options_view()->state().value(g::Option::window_opacity)==0);tick();++cases;
    const auto stable=app.preferences();const auto writes=settings->writes;settings->fail=true;rejects([&]{click(62,335);});++guards;settings->fail=false;CHECK(app.preferences()==stable && scene->preferences()==stable && app.options_view()->state()==g::options_state(stable) && settings->writes==writes);++cases;
    click(62,335);CHECK(app.preferences().window_alpha==256 && settings->writes==writes+1);++cases;
    const auto unsupported=app.preferences();rejects([&]{click(10);});++guards;CHECK(app.preferences()==unsupported && app.options_view()->state()==g::options_state(unsupported));++cases;
    rejects([&]{app.prepare_display({});});rejects([&]{app.prepare_errors({});});guards+=2;
    std::vector<bool> display_changes;bool host_fullscreen{},display_failure{};app.prepare_display([&](bool value){if(display_failure)throw std::runtime_error("owned display switch failed");host_fullscreen=value;display_changes.push_back(value);});CHECK(!host_fullscreen && display_changes.size()==1);++cases;
    click(10);CHECK(host_fullscreen && app.preferences().fullscreen && scene->preferences().fullscreen && g::Preferences::decode(*settings->bytes).fullscreen && display_changes.size()==2);++cases;
    const auto display_writes=settings->writes;settings->fail=true;rejects([&]{click(11);});++guards;settings->fail=false;CHECK(host_fullscreen && app.preferences().fullscreen && app.options_view()->state()==g::options_state(app.preferences()) && settings->writes==display_writes && display_changes.size()==4 && !display_changes[2] && display_changes[3]);++cases;
    display_failure=true;rejects([&]{click(11);});++guards;display_failure=false;CHECK(host_fullscreen && app.preferences().fullscreen && settings->writes==display_writes && display_changes.size()==4);click(11);CHECK(!host_fullscreen && !app.preferences().fullscreen && !g::Preferences::decode(*settings->bytes).fullscreen);++cases;
    rejects([&]{app.prepare_window_sizes({});});++guards;int host_size=-1;bool resize_failure{};std::vector<int> sizes;
    app.prepare_window_sizes([&](int size){if(resize_failure)throw std::runtime_error("owned resize failed");host_size=size;sizes.push_back(size);});CHECK(host_size==0 && sizes==std::vector{0});
    CHECK(app.user_command(g::UserCommand::window_small,now) && host_size==2 && app.preferences().window_size==2 && (app.user_menu().checked&g::command_bit(g::UserCommand::window_small)));CHECK(app.user_command(g::UserCommand::fullscreen,now) && host_fullscreen);++cases;
    const auto before_resize=app.preferences();resize_failure=true;rejects([&]{app.user_command(g::UserCommand::window_medium,now);});resize_failure=false;CHECK(app.preferences()==before_resize && host_fullscreen && host_size==2);++guards;++cases;
    settings->fail=true;rejects([&]{app.user_command(g::UserCommand::window_medium,now);});settings->fail=false;CHECK(app.preferences()==before_resize && host_fullscreen && host_size==2 && sizes==std::vector<int>({0,2,1,2}));++guards;++cases;
    CHECK(app.user_command(g::UserCommand::window_hd,now) && !host_fullscreen && host_size==0 && app.preferences().window_size==0 && g::Preferences::decode(*settings->bytes)==app.preferences());++cases;
    app.background(true,now);const auto paused=app.options_view()->phase();const auto last_frame_size=app.options_view()->sprites().size();for(unsigned n=0;n<10;++n)tick();CHECK(app.options_view()->phase()==paused && app.options_view()->sprites().size()==last_frame_size && scene->statistics().ticks==frozen_ticks && mixer->paused());app.background(false,now);tick();CHECK(!mixer->paused());++cases;
    click(1);until([&]{return app.options_view()->phase()==g::OptionsPhase::active;});CHECK(app.options_view()->page()==1);click(33);CHECK(app.preferences().audio_muted[0] && scene->preferences().audio_muted[0]);click(34);CHECK(!app.preferences().audio_muted[0]);++cases;
    rejects([&]{click(22);});++guards;CHECK(app.preferences().right_click==0);click(3);click(55);click(65);CHECK((commands==std::vector{g::OptionsCommand::Kind::reset,g::OptionsCommand::Kind::preview_effects,g::OptionsCommand::Kind::preview_voice}));++cases;
    click(0);until([&]{return app.options_view()->phase()==g::OptionsPhase::active;});click(50);CHECK(!app.preferences().transition_wait);g::Input cancel;cancel.cancel=true;tick(cancel);CHECK(app.page()==g::Page::options && app.options_view()->phase()==g::OptionsPhase::closing);tick();CHECK(app.page()==g::Page::story && !app.options_view() && scene->statistics().ticks==frozen_ticks && app.mixer()==mixer);++cases;
    frame.clear();CHECK(!owner.expired() && !retained.overlays().empty());retained.clear();images.clear_cache();CHECK(owner.expired());++cases;
    tick();CHECK(scene->statistics().ticks==frozen_ticks+1);const auto saved=app.preferences();CHECK(g::PreferenceStore(settings).current()==saved);++cases;
    missing=true;const auto navigation=std::vector(app.navigation().begin(),app.navigation().end());const auto before=scene->checkpoint();rejects([&]{app.open(g::Page::options,now);});++guards;missing=false;CHECK(app.page()==g::Page::story && !app.options_view() && scene->checkpoint()==before && std::equal(navigation.begin(),navigation.end(),app.navigation().begin(),app.navigation().end()));++cases;
    app.open(g::Page::save,now);rejects([&]{app.open(g::Page::options,now);});++guards;CHECK(app.page()==g::Page::save && !app.options_view());app.back(now);++cases;
    app.prepare_options(load);app.open(g::Page::options,now);until([&]{return app.options_view()->phase()==g::OptionsPhase::active;});rejects([&]{click(3);});++guards;CHECK(app.page()==g::Page::options);app.back(now);CHECK(!app.options_view());++cases;
    app.open(g::Page::options,now);app.start({},now);CHECK(!app.options_view() && app.page()==g::Page::story && app.mixer()!=mixer && app.preferences()==saved);++cases;
    app.open(g::Page::options,now);app.close(now);CHECK(!app.options_view() && app.page()==g::Page::closed && !app.mixer());++cases;
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.native-options-application.v1\",\"cases\":"<<cases<<",\"guards\":"<<guards<<",\"source_backends_shared\":true,\"display_write_failure_rolls_back\":true,\"modal_story_frozen\":true,\"same_mixer_on_return\":true,\"preferences_persisted_before_apply\":true,\"held_slider_input_bound\":true,\"unsupported_actions_explicit\":true,\"synthetic_resources_only\":true,\"original_resources_opened\":false,\"devices_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";CHECK(out);std::cout<<"PASS common native options application ownership, rendering, persistence and story return.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
