#include "native_scene_fixture.hpp"
#include <atomic>

// Count only reads from constructed PAK bytes. No file, device or real scene
// is opened; a warm cache must remain usable when new I/O is denied.
struct Counted:r::Source {
    std::shared_ptr<::Source> data;std::atomic<unsigned> reads{};std::atomic<bool> denied{};
    explicit Counted(std::shared_ptr<::Source> value):data(std::move(value)){}
    std::uint64_t size()const override{return data->size();}
    void read_at(std::uint64_t at,std::span<std::uint8_t> out)override{++reads;if(denied.load())throw r::Error("constructed cache source read denied");data->read_at(at,out);}
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
    std::ofstream report(argv[1]);report<<"{\"schema\":\"wa2.native-loading-cache.v1\",\"cases\":"<<cases<<",\"guards\":"<<guards<<",\"warm_reads_avoided\":"<<warm_reads_avoided<<",\"bounded_image_retention\":true,\"lru_and_entry_limits\":true,\"style_and_archive_isolation\":true,\"active_pixels_survive_eviction\":true,\"active_program_reused\":true,\"program_cache_retains_no_runtime\":true,\"synthetic_resources_only\":true,\"original_resources_opened\":false,\"devices_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";CHECK(report);
    std::cout<<"PASS bounded native image retention and active immutable script reuse; no devices or original resources.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
