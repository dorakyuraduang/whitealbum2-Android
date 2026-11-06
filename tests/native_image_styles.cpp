#include "image_colours.hpp"
#include "resource_catalog.hpp"
#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <thread>

namespace i=wa2::image;namespace r=wa2::resource;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
template<class F>void rejects(F&& f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}CHECK(caught);}
struct Bytes:r::Source {
    std::vector<std::uint8_t> bytes;std::atomic<unsigned> reads{};
    std::uint64_t size()const override{return bytes.size();}
    void read_at(std::uint64_t at,std::span<std::uint8_t> out)override{if(at>bytes.size() || out.size()>bytes.size()-at)throw r::Error("Colour fixture bounds");++reads;if(!out.empty())std::memcpy(out.data(),bytes.data()+at,out.size());}
};
void put(std::vector<std::uint8_t>& b,std::size_t at,std::uint32_t v){for(unsigned n=0;n<4;++n)b.at(at+n)=std::uint8_t(v>>(8*n));}
std::shared_ptr<Bytes> archive(std::string_view name,std::span<const std::uint8_t> bytes){auto result=std::make_shared<Bytes>();auto& b=result->bytes;b.resize(60+bytes.size());std::memcpy(b.data(),"KCAP",4);put(b,12,1);std::memcpy(b.data()+20,name.data(),name.size());put(b,52,60);put(b,56,std::uint32_t(bytes.size()));std::copy(bytes.begin(),bytes.end(),b.begin()+60);return result;}
std::shared_ptr<const i::Raster> await(const std::shared_ptr<i::ImageRequest>& request){const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    do{if(auto value=request->get())return value;std::this_thread::sleep_for(std::chrono::milliseconds(1));}while(std::chrono::steady_clock::now()<until);throw std::runtime_error("Synthetic colour image request timed out");}
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{},guards{};
    const std::vector<std::uint8_t> input{0,0,0,17,255,255,255,37,255,0,0,57,0,255,0,77,0,0,255,97,80,40,120,117};
    // RGBA equivalents of the constructed cached 00438820 pixel oracle.
    const std::array<int,7> modes{0,1,64,128,254,255,256};
    const std::array<std::array<std::uint8_t,18>,7> expected{{
        {0,0,0,255,255,255,76,76,76,150,150,150,27,27,27,60,60,60},
        {0,0,0,253,253,253,75,75,75,148,148,148,26,26,26,59,59,59},
        {0,0,0,253,253,253,119,56,56,111,174,111,20,20,83,64,54,74},
        {0,0,0,253,253,253,164,37,37,74,201,74,13,13,140,69,49,89},
        {0,0,0,253,253,253,253,0,0,0,253,0,0,0,253,79,39,119},
        {0,0,0,254,254,254,254,0,0,0,254,0,0,0,254,79,39,119},
        {0,0,0,255,255,255,255,0,0,0,255,0,0,0,255,80,40,120}}};
    for(unsigned n=0;n<modes.size();++n){i::Raster pixels{6,1,input};i::apply_image_style(pixels,{modes[n]});
        for(unsigned p=0;p<6;++p){for(unsigned c=0;c<3;++c)CHECK(pixels.rgba[p*4+c]==expected[n][p*3+c]);CHECK(pixels.rgba[p*4+3]==input[p*4+3]);}++cases;}
    std::vector<std::uint8_t> one(256),three(768),five(1280);for(unsigned v=0;v<256;++v){one[v]=std::uint8_t(255-v);
        three[v]=std::uint8_t(255-v);three[256+v]=std::uint8_t(v/2);three[512+v]=std::uint8_t(std::min(255u,v*2));
        five[v]=std::uint8_t(v/2);five[256+v]=std::uint8_t(255-v);five[512+v]=std::uint8_t(v);five[768+v]=std::uint8_t(v/2);five[1024+v]=0x5a;}
    auto map=std::make_shared<const i::ColourMap>(i::parse_colour_map(one));i::Raster pixels{1,1,{80,40,120,117}};i::apply_image_style(pixels,{256,map});CHECK(pixels.rgba==std::vector<std::uint8_t>({175,215,135,117}));++cases;
    map=std::make_shared<const i::ColourMap>(i::parse_colour_map(three));pixels.rgba={80,40,120,117};i::apply_image_style(pixels,{256,map});CHECK(pixels.rgba==std::vector<std::uint8_t>({175,20,240,117}));++cases;
    pixels.rgba={80,40,120,117};i::apply_image_style(pixels,{0,map});CHECK(pixels.rgba==std::vector<std::uint8_t>({195,30,120,117}));++cases;
    const auto combined=i::parse_colour_map(five);map=std::make_shared<const i::ColourMap>(combined);pixels.rgba={80,40,120,117};i::apply_image_style(pixels,{256,map});CHECK(pixels.rgba==std::vector<std::uint8_t>({87,20,30,117}));++cases;
    std::fill(five.begin()+1024,five.end(),0xa5);CHECK(i::parse_colour_map(five).channels==combined.channels);++cases;
    for(auto size:{0u,255u,257u,767u,769u,1279u,1281u}){std::vector<std::uint8_t> bad(size);rejects([&]{i::parse_colour_map(bad);});++guards;}
    for(auto chroma:{-1,257}){const auto before=pixels.rgba;rejects([&]{i::apply_image_style(pixels,{chroma});});CHECK(pixels.rgba==before);++guards;}
    auto bad=pixels;bad.width=2;rejects([&]{i::apply_image_style(bad,{});});CHECK(bad.rgba==pixels.rgba);++guards;
    std::vector<std::uint8_t> tga(26);tga[2]=2;tga[12]=2;tga[14]=1;tga[16]=32;tga[17]=40;
    const std::array<std::uint8_t,8> bgra{0,0,255,17,120,40,80,117};std::copy(bgra.begin(),bgra.end(),tga.begin()+18);
    auto image_source=archive("fixture.tga",tga),map_source=archive("tone.amp",one);unsigned map_opens{};
    r::Catalog resources([&](std::string_view path)->std::shared_ptr<r::Source>{if(path=="bak.pak")return image_source;if(path=="grp.pak"){++map_opens;return map_source;}return {};});
    auto normal=resources.image("bak","fixture.tga"),grey=resources.image("bak","fixture.tga",0),tinted=resources.image("bak","fixture.tga",255);
    CHECK(normal!=grey && grey!=tinted && resources.image("bak","FIXTURE.TGA")==normal);const auto normal_pixels=await(normal);CHECK(normal_pixels->rgba==std::vector<std::uint8_t>({255,0,0,17,80,40,120,117}));++cases;
    CHECK(await(grey)->rgba==std::vector<std::uint8_t>({76,76,76,17,60,60,60,117}) && await(tinted)->rgba==std::vector<std::uint8_t>({254,0,0,17,79,39,119,117}));++cases;
    auto effect=resources.image("bak","fixture.tga",0,"Tone.AMP");CHECK(await(effect)->rgba==std::vector<std::uint8_t>({179,179,179,17,195,195,195,117}));++cases;
    const auto map_reads=map_source->reads.load();CHECK(resources.image("bak","fixture.tga",0,"tone.amp")==effect && map_source->reads==map_reads && map_opens==1);++cases;
    const auto image_reads=image_source->reads.load();normal.reset();CHECK(resources.image("bak","fixture.tga")->get()==normal_pixels && image_source->reads==image_reads);++cases;
    rejects([&]{resources.image("bak","fixture.tga",-1);});++guards;
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.native-image-styles.v1\",\"cases\":"<<cases<<",\"guards\":"<<guards<<",\"synthetic_only\":true,\"devices_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";out.close();CHECK(out);
    std::cout<<"PASS native image chroma, AMP effects and styled request caching.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
