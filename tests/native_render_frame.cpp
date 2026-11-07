#include "render_frame.hpp"
#include "application.hpp"
#include "font_glyphs.hpp"
#include "native_scene_fixture.hpp"
#include <array>

struct Settings:g::PreferenceStorage {std::optional<Bytes> read()override{return {};}void replace(std::span<const std::uint8_t>)override{}};
struct Saves:g::SaveStorage {std::optional<Bytes> read(unsigned)override{return {};}void replace(unsigned,std::span<const std::uint8_t>)override{}std::vector<g::SaveSlotInfo> list()override{return {};}};
Bytes font(){const auto width=3200u,height=40*(wa2::text::glyph_count()/80+1);Bytes bytes(21+width*height);bytes[1]=bytes[2]=1;bytes[5]=1;bytes[7]=24;bytes[12]=std::uint8_t(width);bytes[13]=std::uint8_t(width>>8);bytes[14]=std::uint8_t(height);bytes[15]=std::uint8_t(height>>8);bytes[16]=8;bytes[17]=32;bytes[18]=bytes[19]=bytes[20]=255;return bytes;}
s::CommandResult invoke(s::SceneDialogue& dialogue,unsigned id,std::initializer_list<s::Value> args){std::vector<s::Value> values(args);s::Command call{id,values};return *dialogue.dispatch(call);}
template<class F>void until(F&& predicate){const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);unsigned steps{};while(!predicate()){CHECK(++steps<500 && std::chrono::steady_clock::now()<deadline);std::this_thread::sleep_for(std::chrono::milliseconds(1));}}
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{},guards{};g::RenderFrame frame;
    auto image=std::make_shared<i::Raster>(i::Raster{1,1,{255,255,255,255}});
    s::BackgroundFrame background;background.current=s::BackgroundLayer{image,0,0,1280,720,255};background.transform.x=17;
    i::Sprite world{image,0,0,1280,720,0,0,1,1};world.layer=2;
    std::array<i::Sprite,6> items;constexpr int layers[]{0x2000,0x1059,0x1062,0x104d,0x1057,0x105b};
    for(unsigned n=0;n<items.size();++n){items[n]={image,float(n),0,1,1,0,0,1,1};items[n].layer=layers[n];items[n].order=int(n);items[n].space=n==3 || n==4 || n==5?i::SpriteSpace::dialogue:i::SpriteSpace::screen;}
    const std::array groups{std::span<const i::Sprite>(items)};const i::SceneTransform shake{23,41,1.25f,12};
    frame.compose(background,{&world,1},groups,shake);CHECK(frame.background().transform.x==17 && frame.environment().size()==1 && frame.overlays().size()==6 && frame.dialogue_transform()==shake);++cases;
    constexpr unsigned expected[]{3,4,1,5,2,0};for(unsigned n=0;n<6;++n)CHECK(frame.overlays()[n].x==expected[n]);++cases;
    CHECK(frame.environment()[0].image==image && frame.overlays()[0].image==image && frame.overlays()[0].image->rgba.data()==image->rgba.data());++cases;
    items[0].x=99;world.x=99;CHECK(frame.overlays().back().x==0 && frame.environment()[0].x==0);++cases;
    CHECK(frame.overlays()[0].space==i::SpriteSpace::dialogue && frame.overlays()[2].space==i::SpriteSpace::screen);++cases;
    const auto retained=frame;frame.clear();CHECK(frame.overlays().empty() && frame.environment().empty() && !frame.background().current && !frame.movie_active() && retained.overlays().size()==6);++cases;
    std::array<i::Sprite,48> pieces;for(unsigned n=0;n<pieces.size();++n){pieces[n]=items[0];pieces[n].layer=0x1057;pieces[n].order=0x11;pieces[n].x=float(n);}
    const std::array ties{std::span<const i::Sprite>(pieces).first(24),std::span<const i::Sprite>(pieces).last(24)};
    frame.compose({}, {},ties);for(unsigned n=0;n<pieces.size();++n)CHECK(frame.overlays()[n].x==n);++cases;
    auto owner=std::make_shared<std::array<std::uint8_t,6>>();std::weak_ptr<const void> weak=owner;
    wa2::movie::VideoFrame video;video.owner=owner;video.width=video.height=2;
    rejects([&]{frame.compose({}, {},{}, {},false,video);});CHECK(frame.overlays().size()==48);++guards;++cases;
    frame.compose(background,{&world,1},groups,{},true,video);CHECK(frame.movie_active() && frame.movie()->owner==owner && !frame.background().current && frame.environment().empty() && frame.overlays().size()==6);++cases;
    video={};owner.reset();CHECK(!weak.expired());frame.clear();CHECK(weak.expired());++cases;
    auto banks=memory_banks();auto storage=std::make_shared<Storage>();auto progress=std::make_shared<p::Store>(storage);
    const r::BankOpener open=[&](auto name)->std::shared_ptr<r::Source>{const auto found=banks.find(std::string(name));return found==banks.end()?nullptr:found->second;};
    g::Application app(open,progress,std::make_shared<Settings>(),std::make_shared<Saves>(),memory_resolver());
    rejects([&]{frame.capture(app);});CHECK(app.page()==g::Page::title && frame.overlays().empty());++guards;++cases;
    app.open(g::Page::options,0);rejects([&]{frame.capture(app);});CHECK(app.page()==g::Page::options && !frame.movie_active());++guards;++cases;app.back(0);
    // The only executed story is this constructed empty-message/choice script.
    // In particular, no original story, image, font, movie or PCM is opened.
    Script script;script.call(146,{s::Value(1),s::Value(1),s::Value(0),s::Value(8),s::Value(0),s::Value(0),s::Value(0),s::Value(1280.f),s::Value(720.f)});
    for(int n=0;n<3;++n)script.call(176,{s::Value(n),s::Value(std::string("atlas.tga")),s::Value(24+n*7),s::Value(1),s::Value(std::string()),s::Value(256),s::Value(std::string("grp")),s::Value(1)});
    script.call(131,{s::Value(std::string()),s::Value(7),s::Value(1)});for(unsigned n=0;n<2;++n)script.call(208,{s::Value(std::string()),s::Value(0),s::Value(0),s::Value(0)});script.call(209,{s::Value(0)});script.call(194,{s::Value(1000),s::Value(0),s::Value(0)});script.finish();
    banks["ck-gal.pak"]=bank({{"Global.vrb",text("3,FLG_00,0\r\n")},{"LFScriptFunc.fnc",functions(0,200)},{"LFScriptFuncEx.fnc",functions(200,239)},{"1001.bnr",script.code},{"1001.txt",text(script.strings)}});
    app.start({},0);std::int64_t now{};until([&]{now+=16667;app.scene()->update(now,{true});return app.scene()->choices().active();});for(unsigned n=0;n<32;++n)app.scene()->update(now+=16667);
    // Logical ticks do not guarantee a background worker has published its
    // images. Wait for the owned resources used by the geometry assertions.
    until([&]{app.scene()->update(now+=16667);return app.scene()->background().current && app.scene()->foreground().size()>=3 && app.scene()->selection().size()>=2;});
    const auto before=app.scene()->checkpoint();frame.capture(app);CHECK(frame.background().current && frame.overlays().size()>=17 && !frame.movie_active());++cases;
    const auto has=[&](int layer,int order){return std::find_if(frame.overlays().begin(),frame.overlays().end(),[&](const auto& item){return item.layer==layer && item.order==order;});};
    CHECK(has(0x104d,0x11)!=frame.overlays().end() && has(0x1054,0x12)!=frame.overlays().end() && has(0x105b,0x13)!=frame.overlays().end() && has(0x1057,0x454)!=frame.overlays().end());++cases;
    CHECK(has(0x104d,0x40d)->space==i::SpriteSpace::dialogue && has(0x104e,0x418)->space==i::SpriteSpace::screen && has(0x104f,0x417)->space==i::SpriteSpace::dialogue);++cases;
    for(unsigned n=0;n<3;++n)frame.capture(app);CHECK(app.scene()->checkpoint()==before);++cases;
    std::weak_ptr<const i::Raster> saved_image=frame.overlays().front().image;app.close(now);CHECK(!saved_image.expired() && frame.overlays().size()>=17);++cases;frame.capture(app);CHECK(frame.overlays().empty() && !frame.background().current && saved_image.expired());++cases;
    // Materialize only generated constant-coverage fonts to verify body/name
    // group tagging and the alternating source name slots through a save.
    auto font_bytes=font();auto font_bank=bank({{std::string("\x96\x7b\x91\xcc\x38\x30.tga",10),font_bytes},{std::string("\x91\xdc\x89\x65\x38\x30.tga",10),std::move(font_bytes)}});
    wa2::text::FontBook fonts(r::Archive::open(font_bank));r::Catalog resources(open);
    const auto loader=[&](auto name){return resources.image("grp",name);};s::SceneDialogue dialogue(fonts,loader),restored(fonts,loader);
    invoke(dialogue,144,{s::Value(std::string("A"))});invoke(dialogue,131,{s::Value(std::string("B")),s::Value(1),s::Value(1)});until([&]{dialogue.update();return dialogue.ready();});for(unsigned n=0;n<12;++n)dialogue.update();dialogue.advance();
    auto sprites=dialogue.sprites();CHECK(std::count_if(sprites.begin(),sprites.end(),[](const auto& item){return item.layer==0x1057 && item.order==0x443 && item.space==i::SpriteSpace::dialogue;})==2);++cases;
    CHECK(std::count_if(sprites.begin(),sprites.end(),[](const auto& item){return item.layer==0x105b && item.order==0x444;})==2);++cases;
    invoke(dialogue,144,{s::Value(std::string("C"))});for(unsigned n=0;n<4;++n)dialogue.update();const auto saved=dialogue.checkpoint();restored.restore(saved);restored.prepare_restored_assets(resources,memory_resolver()(resources));
    const auto compare_names=[&]{const auto left=dialogue.sprites(),right=restored.sprites();CHECK(left.size()==right.size());unsigned names{};for(unsigned n=0;n<left.size();++n){CHECK(left[n].layer==right[n].layer && left[n].order==right[n].order && left[n].alpha==right[n].alpha && left[n].x==right[n].x && left[n].space==right[n].space);names+=left[n].layer==0x105b;}return names;};
    CHECK(compare_names()==4 && dialogue.checkpoint()==restored.checkpoint());++cases;
    for(unsigned n=0;n<8;++n){dialogue.update();restored.update();compare_names();}invoke(dialogue,144,{s::Value(std::string("D"))});invoke(restored,144,{s::Value(std::string("D"))});dialogue.update();restored.update();CHECK(compare_names()==4 && dialogue.checkpoint()==restored.checkpoint());++cases;
    s::DialogueSettings settings=dialogue.settings();settings.novel_alpha=127;dialogue.settings(settings);invoke(dialogue,220,{s::Value(1)});sprites=dialogue.sprites();CHECK(sprites.front().layer==0x104c && sprites.front().space==i::SpriteSpace::screen && std::none_of(sprites.begin(),sprites.end(),[](const auto& item){return item.layer==0x105b || item.order==0x40d;}));++cases;
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.native-render-frame.v1\",\"cases\":"<<cases<<",\"guards\":"<<guards<<",\"source_layer_order\":true,\"equal_keys_stable\":true,\"owned_snapshot\":true,\"capture_does_not_advance_logic\":true,\"synthetic_resources_only\":true,\"original_resources_opened\":false,\"devices_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";CHECK(out);std::cout<<"PASS common native frame assembly, UI layers, name-slot restore and snapshot ownership.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
