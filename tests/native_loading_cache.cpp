#include "native_scene_fixture.hpp"
#include "font_glyphs.hpp"
#include <atomic>
#include <tuple>

// Count only reads from constructed PAK bytes. No file, device or real scene
// is opened; a warm cache must remain usable when new I/O is denied.
struct Counted:r::Source {
    std::shared_ptr<::Source> data;std::atomic<unsigned> reads{};std::atomic<bool> denied{};
    explicit Counted(std::shared_ptr<::Source> value):data(std::move(value)){}
    std::uint64_t size()const override{return data->size();}
    void read_at(std::uint64_t at,std::span<std::uint8_t> out)override{++reads;if(denied.load())throw r::Error("constructed cache source read denied");data->read_at(at,out);}
};
// Indexed marker sheets synthesized from headers and a uniform palette. They
// contain no original font data and need no full-size source-file allocation.
struct MarkerFonts:r::Source {
    Bytes prefix,font;std::vector<std::uint64_t> starts;std::uint64_t length{};unsigned reads{};bool denied{};
    MarkerFonts(std::vector<std::string> names,std::uint8_t coverage){const auto width=3200u,height=40u*(wa2::text::glyph_count(true)/80+1),bytes=21+width*height;CHECK(bytes<=16u*1024*1024);prefix.resize(16+names.size()*44);std::memcpy(prefix.data(),"KCAP",4);put(prefix,12,unsigned(names.size()));
        for(unsigned n=0;n<names.size();++n){starts.push_back(prefix.size()+std::uint64_t(n)*bytes);std::memcpy(prefix.data()+20+n*44,names[n].data(),names[n].size());put(prefix,52+n*44,unsigned(starts.back()));put(prefix,56+n*44,bytes);}length=prefix.size()+names.size()*std::uint64_t(bytes);
        font.resize(21);font[1]=font[2]=font[5]=1;font[7]=24;font[12]=std::uint8_t(width);font[13]=std::uint8_t(width>>8);font[14]=std::uint8_t(height);font[15]=std::uint8_t(height>>8);font[16]=8;font[17]=32;font[18]=font[19]=font[20]=coverage;}
    std::uint64_t size()const override{return length;}
    void read_at(std::uint64_t at,std::span<std::uint8_t> out)override{++reads;if(denied)throw r::Error("owned font read denied");CHECK(at<=length && out.size()<=length-at);std::fill(out.begin(),out.end(),0);const auto copy=[&](std::uint64_t start,const Bytes& data){const auto first=std::max(at,start),last=std::min(at+out.size(),start+data.size());if(first<last)std::copy_n(data.data()+std::size_t(first-start),std::size_t(last-first),out.data()+std::size_t(first-at));};copy(0,prefix);for(auto start:starts)copy(start,font);}
};
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{},guards{},warm_reads_avoided{};
    const auto source=std::make_shared<Counted>(bank({{"a.tga",raster(4,4)},{"b.tga",raster(4,4)},{"c.tga",raster(4,4)},{"large.tga",raster(8,8)},{"bad.tga",Bytes{1,2,3}}}));
    const auto archive=r::Archive::open(source);
    i::ImageStore images(archive,{},i::ImageCacheLimits{128,2,64});
    const auto load=[&](std::string_view name,i::ImageStyle style={}){auto request=images.request(name,style);auto pixels=request->wait_for(std::chrono::seconds(2));CHECK(pixels);return pixels;};
    auto first=load("a.tga");const auto expected=first->rgba;std::weak_ptr<const i::Raster> identity=first;first.reset();
    CHECK(images.cache_statistics().retained_bytes==64 && images.cache_statistics().retained_entries==1);++cases;
    auto reads=source->reads.load();source->denied=true;auto warm=images.request("A.TGA");CHECK(warm->get()==identity.lock() && warm->get()->rgba==expected && source->reads==reads);++warm_reads_avoided;++cases;source->denied=false;warm.reset();
    auto second=load("b.tga");second.reset();auto touched=load("a.tga");touched.reset();auto third=load("c.tga");third.reset();
    auto state=images.cache_statistics();CHECK(state.retained_bytes==128 && state.retained_entries==2 && state.evictions==1);++cases;
    reads=source->reads;source->denied=true;CHECK(load("a.tga")->rgba==expected && source->reads==reads);++warm_reads_avoided;source->denied=false;
    reads=source->reads;auto reloaded=load("b.tga");CHECK(source->reads>reads && reloaded->rgba==expected);++cases;
    // An oversize decode must neither consume retention space nor evict a hot
    // small image. Active callers still own its complete immutable pixels.
    const auto before=images.cache_statistics();auto large=load("large.tga");CHECK(large->rgba.size()==256);state=images.cache_statistics();CHECK(state.retained_bytes==before.retained_bytes && state.retained_entries==before.retained_entries && state.evictions==before.evictions);++cases;
    reads=source->reads;source->denied=true;CHECK(load("b.tga")==reloaded && source->reads==reads);++warm_reads_avoided;source->denied=false;++cases;
    // Colour/chroma variants have separate keys and exact independently
    // transformed bytes, while resource descriptions keep the original style.
    auto grey=load("b.tga",{0});CHECK(grey!=reloaded && grey->rgba!=expected);auto record=images.describe(grey);CHECK(record && record->member=="b.tga" && record->style.chroma==0);++cases;
    auto colours=std::make_shared<i::ColourMap>();for(unsigned n=0;n<256;++n){colours->channels[0][n]=std::uint8_t(255-n);colours->channels[1][n]=std::uint8_t(n);colours->channels[2][n]=std::uint8_t(n);}
    auto tinted=load("b.tga",{256,colours});CHECK(tinted!=grey && tinted!=reloaded && tinted->rgba[0]==64 && tinted->rgba[1]==57 && tinted->rgba[2]==31 && tinted->rgba[3]==255);++cases;
    reads=source->reads;source->denied=true;CHECK(load("b.tga",{256,colours})==tinted && source->reads==reads);++warm_reads_avoided;source->denied=false;++cases;
    const auto stable=images.cache_statistics();auto broken=images.request("bad.tga");rejects([&]{broken->wait_for(std::chrono::seconds(2));});broken.reset();state=images.cache_statistics();CHECK(state.retained_bytes==stable.retained_bytes && state.evictions==stable.evictions);++guards;++cases;
    auto distinct_source=std::make_shared<Counted>(bank({{"b.tga",raster(4,4)}}));i::ImageStore distinct(r::Archive::open(distinct_source),{},i::ImageCacheLimits{128,2,64});auto foreign=distinct.request("b.tga")->wait_for(std::chrono::seconds(2));CHECK(foreign && foreign!=reloaded && foreign->rgba==reloaded->rgba && !images.describe(foreign));++cases;
    // Pixel-byte and entry limits are independent. Live users remain valid
    // after an eviction and after the entire loader is destroyed.
    {auto limited=std::make_unique<i::ImageStore>(archive,std::function<void()>{},i::ImageCacheLimits{1024,1,64});auto old=limited->request("a.tga")->wait_for(std::chrono::seconds(2));CHECK(old);auto next=limited->request("b.tga")->wait_for(std::chrono::seconds(2));CHECK(next && limited->cache_statistics().retained_entries==1 && limited->cache_statistics().retained_bytes==64 && limited->cache_statistics().evictions==1);limited.reset();CHECK(old->rgba==expected && next->rgba==expected);}++cases;
    images.clear_cache();CHECK(!images.cache_statistics().retained_bytes && !images.cache_statistics().retained_entries && tinted->rgba[0]==64 && reloaded->rgba==expected);++cases;
    {i::ImageStore disabled(archive,{},i::ImageCacheLimits{0,0,0});auto value=disabled.request("a.tga")->wait_for(std::chrono::seconds(2));CHECK(value && !disabled.cache_statistics().retained_bytes && !disabled.cache_statistics().retained_entries);}++cases;
    for(auto limits:{i::ImageCacheLimits{128,2,129},i::ImageCacheLimits{128,65,64},i::ImageCacheLimits{128,0,64},i::ImageCacheLimits{0,2,0},i::ImageCacheLimits{33u*1024*1024,2,64}}){bool initialized{};rejects([&]{i::ImageStore invalid(archive,[&]{initialized=true;},limits);});CHECK(!initialized);++guards;}++cases;
    // Re-entering a currently owned immutable LSCR Program must not read or
    // parse the same binary/text again. Runtime/global state is not cached.
    Script program;program.finish();auto scripts=std::make_shared<Counted>(bank({{"Global.vrb",text("3,COUNT,0\r\n")},{"LFScriptFunc.fnc",functions(0,200)},{"LFScriptFuncEx.fnc",functions(200,239)},{"a.bnr",program.code},{"a.txt",text(program.strings)},{"b.bnr",program.code},{"b.txt",text(program.strings)}}));
    s::Library library(r::Archive::open(scripts));auto a=library.load("a");CHECK(a && a->name()=="a");reads=scripts->reads;scripts->denied=true;auto a_again=library.load("a");CHECK(a_again==a && scripts->reads==reads);++warm_reads_avoided;++cases;
    library.globals().set(0,s::Value(91));CHECK(library.load("a")==a && library.globals().get(0).integer()==91 && scripts->reads==reads);++warm_reads_avoided; scripts->denied=false;++cases;
    auto b=library.load("b");CHECK(b!=a && b->name()=="b");std::weak_ptr<const s::Program> weak=a;a.reset();a_again.reset();CHECK(weak.expired());reads=scripts->reads;auto fresh=library.load("a");CHECK(fresh && scripts->reads>reads && library.globals().get(0).integer()==91);++cases;
    rejects([&]{library.load("missing");});++guards;reads=scripts->reads;scripts->denied=true;CHECK(library.load("a")==fresh && scripts->reads==reads);scripts->denied=false;++warm_reads_avoided;++cases;
    {
        Script base,patch;for(auto [script,value]:{std::pair{&base,11},std::pair{&patch,22}}){script->call(133,{s::Value(std::string("unused")),s::Value(value),s::Value(1)});script->finish();}
        const auto root=std::make_shared<Counted>(bank({{"Global.vrb",text("3,COUNT,1\r\n")},{"LFScriptFunc.fnc",functions(0,200)},{"LFScriptFuncEx.fnc",functions(200,239)},{"a.bnr",base.code},{"a.txt",text("ROOT,")},{"d.bnr",base.code},{"d.txt",text("BASE D,")},{"b.bnr",base.code},{"b.txt",text("BASE,")}}));
        const auto patched=std::make_shared<Counted>(bank({{"Global.vrb",text("3,COUNT,2\r\n")},{"a.bnr",patch.code},{"a.txt",text("PATCH,")},{"d.bnr",patch.code},{"d.txt",text("PATCH D,")}}));
        const auto intro=std::make_shared<Counted>(bank({{"a.bnr",base.code},{"a.txt",text("IC,")},{"b.txt",text("INTRO,")},{"c.bnr",base.code},{"c.txt",text("CHILD,")}}));
        std::map<std::string,std::shared_ptr<r::Source>> files{{"patch.pak",patched},{"ck-gal.pak",root},{"IC/ck-gal.pak",intro}};std::vector<std::string> opened;
        r::Catalog catalogue([&](auto name)->std::shared_ptr<r::Source>{opened.emplace_back(name);const auto found=files.find(std::string(name));return found==files.end()?nullptr:found->second;});s::Library merged(catalogue.members("CK-GAL"));CHECK(merged.globals().get(0).integer()==2 && opened==std::vector<std::string>({"patch.pak","ck-gal.pak"}));++cases;
        auto program=merged.load("a");CHECK(program->strings()[0]=="PATCH" && opened.size()==2);s::Runtime runtime(program,merged.functions(),merged.globals());CHECK(runtime.start(0));unsigned called{};CHECK(runtime.advance([&](auto& command){CHECK(command.index==133 && command.arguments[0].text()=="PATCH" && command.arguments[1].integer()==22);++called;return s::CommandResult{true,false,{}};})==s::RunState::stopped && called==1);++cases;
        CHECK(merged.load("b")->strings()[0]=="BASE");auto child=merged.load("c");CHECK(child->strings()[0]=="CHILD" && opened==std::vector<std::string>({"patch.pak","ck-gal.pak","IC/ck-gal.pak"}));++cases;
        const auto root_reads=root->reads.load(),patch_reads=patched->reads.load(),intro_reads=intro->reads.load();root->denied=patched->denied=intro->denied=true;CHECK(merged.load("a")==program && merged.load("c")==child && root->reads==root_reads && patched->reads==patch_reads && intro->reads==intro_reads);++warm_reads_avoided;++cases;root->denied=patched->denied=intro->denied=false;
        // A selected corrupt member or PAK and a selected member read failure
        // must not silently select an older script from another installation.
        files["patch.pak"]=bank({{"a.bnr",Bytes{1,2,3}},{"a.txt",text("BAD,")}});r::Catalog malformed([&](auto name)->std::shared_ptr<r::Source>{const auto found=files.find(std::string(name));return found==files.end()?nullptr:found->second;});s::Library bad(malformed.members("ck-gal"));rejects([&]{bad.load("a");});++guards;++cases;
        files["patch.pak"]=std::make_shared<Source>();opened.clear();r::Catalog malformed_bank([&](auto name)->std::shared_ptr<r::Source>{opened.emplace_back(name);const auto found=files.find(std::string(name));return found==files.end()?nullptr:found->second;});rejects([&]{s::Library bad(malformed_bank.members("ck-gal"));});CHECK(opened==std::vector<std::string>({"patch.pak"}));++guards;++cases;
        const auto before_denied_root=root->reads.load(),before_denied_intro=intro->reads.load();patched->denied=true;rejects([&]{merged.load("d");});CHECK(merged.load("a")==program && root->reads==before_denied_root && intro->reads==before_denied_intro);patched->denied=false;++guards;++cases;
        rejects([&]{merged.load("../a");});rejects([&]{s::Library bad(r::MemberLoader{});});rejects([&]{wa2::text::FontBook bad(r::MemberLoader{});});guards+=3;++cases;
        unsigned limit_calls{};s::Library limits([&](auto name,std::size_t maximum){CHECK(maximum==16u*1024*1024);++limit_calls;return name=="Global.vrb"?text("3,COUNT,0\r\n"):functions(name=="LFScriptFunc.fnc"?0:200,name=="LFScriptFunc.fnc"?200:239);});CHECK(limit_calls==3);++cases;
        auto bounded_source=std::make_shared<Counted>(bank({{"large.bin",Bytes(9,1)}}));r::Catalog bounded([&](auto name)->std::shared_ptr<r::Source>{return name=="grp.pak"?bounded_source:nullptr;});CHECK(bounded.optional("grp"));const auto no_payload_read=bounded_source->reads.load();rejects([&]{bounded.members("grp")("large.bin",8);});CHECK(bounded_source->reads==no_payload_read);++guards;++cases;
        auto owned=r::archive_members(r::Archive::open(bank({{"value",Bytes{17}}})));CHECK(owned("value",1)==Bytes{17});rejects([&]{owned("value",0);});rejects([&]{owned("missing",1);});guards+=2;++cases;
        auto scene_banks=memory_banks();scene_banks["patch.pak"]=bank({{"Global.vrb",text("3,FLG_00,2\r\n")},{"1001.bnr",patch.code},{"1001.txt",text("OWNER,")}});auto progress=std::make_shared<p::Store>(std::make_shared<Storage>());g::Scene scene([&](auto name)->std::shared_ptr<r::Source>{const auto found=scene_banks.find(std::string(name));return found==scene_banks.end()?nullptr:found->second;},1,progress);scene.start("1001");CHECK(scene.update(0)==s::RunState::stopped && scene.theme()==2 && scene.backlog().entries().front().text=="OWNER" && scene.backlog().entries().front().line==22);++cases;
    }
    {
        const std::string foreground="\x96\x7b\x91\xcc" "80.tga",shadow="\x91\xdc\x89\x65" "80.tga";
        auto patch=std::make_shared<MarkerFonts>(std::vector{foreground},61),root=std::make_shared<MarkerFonts>(std::vector{foreground,shadow},127),intro=std::make_shared<MarkerFonts>(std::vector{foreground,shadow},191);
        std::map<std::string,std::shared_ptr<r::Source>> files{{"patch.pak",patch},{"fon.pak",root},{"IC/fon.pak",intro}};unsigned intro_opens{};r::Catalog catalogue([&](auto name)->std::shared_ptr<r::Source>{if(name=="IC/fon.pak")++intro_opens;const auto found=files.find(std::string(name));return found==files.end()?nullptr:found->second;});
        wa2::text::FontBook fonts(catalogue.members("fon"));auto prepared=fonts.prepare(wa2::text::layout("A"));std::vector<i::Sprite> sprites;prepared.append(sprites,0,100000,0,0);CHECK(sprites.size()==2 && !intro_opens);const auto pixels=sprites.back().image;CHECK(pixels->rgba[3]==61 && pixels->rgba[40*4+3]==127);++cases;
        const auto patch_reads=patch->reads,root_reads=root->reads;patch->denied=root->denied=true;auto next=fonts.prepare(wa2::text::layout("B"));CHECK(next.image_bytes() && patch->reads==patch_reads && root->reads==root_reads);++warm_reads_avoided;++cases;patch->denied=root->denied=false;
        // Constructed font coverage; expected integer rectangles were checked
        // against cached generated C++ with our own ASCII/control strings.
        // Keep the old layout record round trip while correcting presentation.
        for(const auto [raw,left,top,extent,shadow_extent]:std::array<std::tuple<std::string_view,int,int,int,int>,3>{{{"AB",4,18,32,40},{"<F20:AB>",2,16,23,28},{"<F14:AB>",2,16,16,20}}}){
            const auto metrics=wa2::text::checkpoint_layout(wa2::text::layout(raw));auto text=fonts.prepare(wa2::text::restore_layout(metrics));CHECK(wa2::text::checkpoint_layout(text.metrics())==metrics);sprites.clear();text.append(sprites,0,100000,.25f,3.5f);
            CHECK(sprites.size()==4 && sprites[0].red==0 && sprites[0].green==0 && sprites[0].blue==0 && sprites[2].red==255 && sprites[2].green==255 && sprites[2].blue==255);
            CHECK(sprites[0].x==.25f && sprites[0].y==17.5f && sprites[0].width==shadow_extent && sprites[0].height==shadow_extent && sprites[2].x==left+.25f && sprites[2].y==top+3.5f && sprites[2].width==extent && sprites[2].height==extent && sprites[2].source_width==32);
            const auto advance=raw=="AB"?14:raw=="<F20:AB>"?10:7;CHECK(sprites[1].x==advance+.25f && sprites[3].x==advance+left+.25f && sprites[0].alpha==255 && sprites[2].alpha==255);++cases;
        }
        files.erase("patch.pak");files.erase("fon.pak");r::Catalog intro_only([&](auto name)->std::shared_ptr<r::Source>{const auto found=files.find(std::string(name));return found==files.end()?nullptr:found->second;});wa2::text::FontBook child_fonts(intro_only.members("fon"));auto child=child_fonts.prepare(wa2::text::layout("C"));sprites.clear();child.append(sprites,0,100000,0,0);CHECK(sprites.size()==2 && sprites.back().image->rgba[3]==191 && sprites.back().image->rgba[40*4+3]==191);++cases;
        std::vector<std::size_t> limits;bool fail_shadow=true;wa2::text::FontBook retry([&](auto name,std::size_t maximum){limits.push_back(maximum);if(name==shadow && fail_shadow)throw r::Error("owned shadow failure");return r::archive_members(r::Archive::open(root))(name,maximum);});auto pending=wa2::text::FontBook::defer(wa2::text::layout("D"));rejects([&]{retry.materialize(pending);});CHECK(!pending.image_bytes() && limits==std::vector<std::size_t>({16u*1024*1024,16u*1024*1024}));fail_shadow=false;CHECK(retry.materialize(pending) && pending.image_bytes() && limits.size()==3);++guards;++cases;
    }
    std::ofstream report(argv[1]);report<<"{\"schema\":\"wa2.native-loading-cache.v1\",\"cases\":"<<cases<<",\"guards\":"<<guards<<",\"warm_reads_avoided\":"<<warm_reads_avoided<<",\"bounded_image_retention\":true,\"lru_and_entry_limits\":true,\"style_and_archive_isolation\":true,\"active_pixels_survive_eviction\":true,\"active_program_reused\":true,\"program_cache_retains_no_runtime\":true,\"script_patch_root_ic_order\":true,\"font_patch_root_ic_order\":true,\"selected_failure_preserved\":true,\"member_read_limits\":true,\"synthetic_font_pixels_only\":true,\"synthetic_resources_only\":true,\"original_resources_opened\":false,\"devices_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";CHECK(report);
    std::cout<<"PASS bounded native image retention and active immutable script reuse; no devices or original resources.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
