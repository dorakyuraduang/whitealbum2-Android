#include "application.hpp"
#include "render_frame.hpp"
#include "native_scene_fixture.hpp"
#include "native_save_atlases.hpp"
#include <algorithm>

struct Settings:g::PreferenceStorage {
    std::optional<Bytes> bytes;
    std::optional<Bytes> read()override{return bytes;}
    void replace(std::span<const std::uint8_t> value)override{bytes=Bytes(value.begin(),value.end());}
};
struct Saves:g::SaveStorage {
    std::map<unsigned,Bytes> entries;bool fail{};unsigned writes{},reads{},prefixes{};
    std::optional<Bytes> read(unsigned slot)override{++reads;const auto it=entries.find(slot);return it==entries.end()?std::optional<Bytes>{}:it->second;}
    std::optional<Bytes> prefix(unsigned slot,std::size_t count)override{++prefixes;const auto it=entries.find(slot);if(it==entries.end())return {};return Bytes(it->second.begin(),it->second.begin()+std::min(it->second.size(),count));}
    void replace(unsigned slot,std::span<const std::uint8_t> value)override{if(fail)throw std::runtime_error("owned save write failure");entries[slot]=Bytes(value.begin(),value.end());++writes;}
    std::vector<g::SaveSlotInfo> list()override{std::vector<g::SaveSlotInfo> value;for(const auto& [slot,bytes]:entries)value.push_back({slot,bytes.size(),1000});return value;}
};
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{},guards{},image_requests{},snapshots{},stamps{};bool missing{},bad_snapshot{},failed_restore{};
    auto banks=memory_banks();Script script;script.call(176,{s::Value(0),s::Value(std::string("atlas.tga")),s::Value(24),s::Value(1),s::Value(std::string()),s::Value(256),s::Value(std::string("grp")),s::Value(0)});
    script.call(194,{s::Value(1000000),s::Value(0),s::Value(1)});script.finish();
    banks["ck-gal.pak"]=bank({{"Global.vrb",text("3,FLG_00,0\r\n3,FLG_\x8f\xcd\x8a\xc7\x97\x9d\x83T\x83u,0\r\n")},{"LFScriptFunc.fnc",functions(0,200)},{"LFScriptFuncEx.fnc",functions(200,239)},{"1001.bnr",script.code},{"1001.txt",text(script.strings)}});
    const r::BankOpener opener=[&](auto name)->std::shared_ptr<r::Source>{const auto found=banks.find(std::string(name));return found==banks.end()?nullptr:found->second;};
    std::map<std::string,Bytes> atlases;for(const auto& atlas:wa2::test::save_atlases)atlases[atlas.name]=raster(atlas.w,atlas.h);i::ImageStore images(r::Archive::open(bank(std::move(atlases))));
    auto settings=std::make_shared<Settings>();auto saves=std::make_shared<Saves>();auto progress=std::make_shared<p::Store>(std::make_shared<Storage>());const auto resolver=memory_resolver();
    g::Application app(opener,progress,settings,saves,[&](r::Catalog& catalog)->i::Resolve{if(failed_restore)return [](const auto&)->std::shared_ptr<const i::Raster>{throw std::runtime_error("owned restore image failure");};return resolver(catalog);});
    rejects([&]{app.prepare_saves({});});rejects([&]{app.prepare_save_thumbnail({});});guards+=2;
    g::SaveServices services;services.images=[&](auto name){++image_requests;return missing?std::shared_ptr<i::ImageRequest>{}:images.request(name);};
    services.text=[](auto raw,const auto& style){return wa2::text::FontBook::defer(wa2::text::layout(raw,style));};
    services.stamp=[&]{++stamps;return g::SaveStamp{};};
    const auto thumbnail=std::make_shared<const i::Raster>(i::Raster{128,72,Bytes(128u*72*4,191)});
    services.thumbnail=[&](g::Scene& scene)->std::shared_ptr<const i::Raster>{++snapshots;CHECK(&scene==app.scene());return bad_snapshot?nullptr:thumbnail;};
    app.prepare_saves(services);CHECK(!image_requests && app.has_save_thumbnail() && !snapshots && !saves->writes);++cases;
    rejects([&]{app.open(g::Page::load,0);});CHECK(app.page()==g::Page::title && !app.save_view());++guards;++cases;
    std::int64_t now{};const auto tick=[&](g::Input input={}){now+=16667;return app.frame(now,input,[&]{return now;});};
    const auto until=[&](auto ready){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);unsigned ticks{};while(!ready()){CHECK(++ticks<1200 && std::chrono::steady_clock::now()<end);tick();std::this_thread::sleep_for(std::chrono::milliseconds(1));}};
    const auto click=[&](unsigned id){const auto hits=app.save_view()->hits();const auto hit=std::find_if(hits.begin(),hits.end(),[&](const auto& value){return value.id==id;});CHECK(hit!=hits.end());g::Input input;input.selection={true,true,false,hit->x+1,hit->y+1,0};return tick(input);};
    const auto active=[&]{until([&]{return app.save_view()->phase()==g::SavePhase::active;});};
    app.start({},now);until([&]{return app.scene()->statistics().command==194;});auto* scene=app.scene();auto mixer=app.mixer();const auto frozen=scene->checkpoint();const auto frozen_ticks=scene->statistics().ticks;
    app.open(g::Page::save,now);CHECK(app.save_view() && app.page()==g::Page::save && !snapshots);active();CHECK(scene->checkpoint()==frozen && scene->statistics().ticks==frozen_ticks && app.mixer()==mixer && !mixer->paused());++cases;
    rejects([&]{app.prepare_saves(services);});rejects([&]{app.prepare_save_thumbnail(services.thumbnail);});rejects([&]{app.open(g::Page::load,now);});guards+=3;CHECK(app.save_view()->phase()==g::SavePhase::active);++cases;
    g::RenderFrame frame;frame.capture(app);CHECK(frame.overlays().size()>=app.save_view()->sprites().size() && !frame.movie_active());auto retained=frame;++cases;
    app.background(true,now);g::Input page;page.page_delta=1;tick(page);CHECK(app.save_view()->page()==0 && scene->statistics().ticks==frozen_ticks && mixer->paused());app.background(false,now);tick();CHECK(!mixer->paused());++cases;
    click(0);CHECK(saves->writes==1 && snapshots==1 && stamps==1 && app.save_view()->phase()==g::SavePhase::result && g::SaveDocument::wrapped(saves->entries.at(0)));const auto document=g::SaveDocument::decode(saves->entries.at(0));CHECK(document.snapshot==frozen && document.preview.scene=="1001" && document.preview.story_known && document.preview.thumbnail->rgba==thumbnail->rgba && app.save_view()->rows()[0].occupied);++cases;
    active();tick(page);active();CHECK(app.save_view()->page()==1);page.page_delta=-1;tick(page);active();CHECK(app.save_view()->page()==0 && app.save_view()->rows()[0].occupied && saves->prefixes && !saves->reads);++cases;
    saves->fail=true;rejects([&]{click(1);});++guards;CHECK(app.save_view()->phase()==g::SavePhase::active && app.save_view()->operation_error()=="owned save write failure" && saves->writes==1 && !app.save_view()->rows()[1].occupied && app.scene()==scene && scene->checkpoint()==frozen);saves->fail=false;++cases;
    bad_snapshot=true;rejects([&]{click(1);});++guards;CHECK(saves->writes==1 && !app.save_view()->rows()[1].occupied && app.save_view()->phase()==g::SavePhase::active);bad_snapshot=false;++cases;
    click(1);CHECK(saves->writes==2 && app.save_view()->rows()[1].occupied);active();g::Input cancel;cancel.cancel=true;tick(cancel);CHECK(app.page()==g::Page::save && app.save_view()->phase()==g::SavePhase::closing);until([&]{return app.page()==g::Page::story;});CHECK(!app.save_view() && scene->statistics().ticks==frozen_ticks && app.mixer()==mixer && !retained.overlays().empty());++cases;
    missing=true;rejects([&]{app.open(g::Page::save,now);});++guards;missing=false;CHECK(app.page()==g::Page::story && !app.save_view() && app.scene()==scene);++cases;
    auto preferences=app.preferences();preferences.confirmation=false;preferences.right_click=2;app.preferences(preferences);tick();const auto story_ticks=scene->statistics().ticks;g::Input right;right.toggle_window=true;tick(right);CHECK(app.page()==g::Page::save && app.save_view() && scene->statistics().ticks==story_ticks);active();CHECK(app.back(now) && !app.save_view() && app.mixer()==mixer);++cases;
    app.open(g::Page::load,now);active();const auto old=scene->checkpoint();const auto valid_slot=saves->entries.at(0);saves->entries.at(0).back()^=1;rejects([&]{click(0);});++guards;CHECK(app.scene()==scene && scene->checkpoint()==old && app.save_view()->phase()==g::SavePhase::active && !app.save_view()->operation_error().empty());saves->entries.at(0)=valid_slot;++cases;
    failed_restore=true;rejects([&]{click(0);});++guards;failed_restore=false;CHECK(app.scene()==scene && app.mixer()==mixer && scene->checkpoint()==old && app.page()==g::Page::load && app.save_view()->phase()==g::SavePhase::active);++cases;
    rejects([&]{app.prepare_errors({});});++guards;std::vector<std::string> errors;app.prepare_errors([&](auto message){errors.emplace_back(message);});failed_restore=true;click(0);failed_restore=false;
    CHECK(errors.size()==1 && !errors[0].empty() && app.scene()==scene && app.page()==g::Page::load && app.save_view()->phase()==g::SavePhase::active && app.mixer()==mixer && scene->checkpoint()==old);now+=5000000;tick();CHECK(scene->checkpoint()==old);++cases;
    click(0);CHECK(app.page()==g::Page::story && !app.save_view() && app.scene()!=scene && app.mixer()!=mixer && app.scene_revision()==2 && app.scene()->preferences()==preferences);CHECK(g::SavedScene::decode(app.scene()->checkpoint()).part(g::ScenePart::script)==g::SavedScene::decode(frozen).part(g::ScenePart::script));++cases;
    app.open(g::Page::save,now);active();app.title(now);CHECK(!app.save_view() && app.page()==g::Page::title);app.start({},now);app.open(g::Page::save,now);app.close(now);CHECK(app.page()==g::Page::closed && !app.save_view() && !app.mixer());++cases;
    std::ofstream report(argv[1]);report<<"{\"schema\":\"wa2.native-save-application.v1\",\"cases\":"<<cases<<",\"guards\":"<<guards<<",\"common_save_view_bound\":true,\"failed_load_preserves_scene\":true,\"metadata_and_thumbnail_written\":true,\"recoverable_operation_errors\":true,\"bounded_preview_reads\":true,\"right_click_save_route\":true,\"same_mixer_on_return\":true,\"synthetic_resources_only\":true,\"devices_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";CHECK(report);std::cout<<"PASS common save menu, metadata, failed writes, restore ownership and story return.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
