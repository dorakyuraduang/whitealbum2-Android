#include "raster_image.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>

namespace i=wa2::image;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
template<class F>void rejects(F&& f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}CHECK(caught);}
std::vector<std::uint8_t> fixture(bool top,unsigned entry_bytes){std::vector<std::uint8_t> b(18+4*entry_bytes+12);b[1]=1;b[2]=1;b[5]=4;b[7]=std::uint8_t(entry_bytes*8);b[12]=4;b[14]=3;b[16]=8;b[17]=top?32:0;
    for(unsigned n=0;n<4;++n){b[18+n*entry_bytes]=std::uint8_t(11+n*61);b[19+n*entry_bytes]=std::uint8_t(200-n*9);b[20+n*entry_bytes]=std::uint8_t(3+n*17);if(entry_bytes==4)b[21+n*entry_bytes]=0;}
    for(unsigned n=0;n<12;++n)b[18+4*entry_bytes+n]=std::uint8_t((n+n/4)%4);return b;}
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{},guards{};
    for(bool top:{false,true})for(unsigned entry:{3u,4u}){const auto source=fixture(top,entry);const auto layout=i::inspect_game_tga(source);const i::Region crop{1,1,2,2};
        const auto decoded=i::decode_game_tga_region(source,crop);i::Raster output{7,5,std::vector<std::uint8_t>(7*5*4,17)};i::copy_game_font_coverage(source,layout,crop,output,3,2);
        for(unsigned y=0;y<output.height;++y)for(unsigned x=0;x<output.width;++x)for(unsigned c=0;c<4;++c){const auto value=output.rgba[(y*output.width+x)*4+c];
            if(x>=3 && x<5 && y>=2 && y<4)CHECK(value==(c==3?decoded.rgba[((y-2)*2+x-3)*4+2]:255));else CHECK(value==17);}
        ++cases;
    }
    auto source=fixture(true,4);const auto layout=i::inspect_game_tga(source);i::Raster output{7,5,std::vector<std::uint8_t>(7*5*4,17)};
    i::copy_game_font_coverage(source,layout,{4,3,0,0},output,7,5);CHECK(std::all_of(output.rgba.begin(),output.rgba.end(),[](auto value){return value==17;}));++cases;
    auto wrong=layout;wrong.bits=24;rejects([&]{i::copy_game_font_coverage(source,wrong,{0,0,1,1},output,0,0);});++guards;
    wrong=layout;wrong.palette_entries=257;rejects([&]{i::copy_game_font_coverage(source,wrong,{0,0,1,1},output,0,0);});++guards;
    rejects([&]{i::copy_game_font_coverage(std::span(source).first(source.size()-1),layout,{0,0,1,1},output,0,0);});++guards;
    rejects([&]{i::copy_game_font_coverage(source,layout,{3,2,2,2},output,0,0);});++guards;
    rejects([&]{i::copy_game_font_coverage(source,layout,{0,0,2,2},output,6,4);});++guards;
    auto invalid=output;invalid.rgba.pop_back();rejects([&]{i::copy_game_font_coverage(source,layout,{0,0,1,1},invalid,0,0);});++guards;
    source[layout.data_offset]=4;rejects([&]{i::copy_game_font_coverage(source,layout,{0,0,1,1},output,0,0);});++guards;
    std::ofstream report(argv[1]);report<<"{\"schema\":\"wa2.native-font-coverage.v1\",\"cases\":"<<cases<<",\"guards\":"<<guards<<",\"matches_rgba_region_blue\":true,\"palette_alpha_ignored\":true,\"synthetic_only\":true,\"devices_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";
    std::cout<<"PASS direct native glyph coverage and existing decoder equivalence.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
