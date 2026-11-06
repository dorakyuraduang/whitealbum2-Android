#include "sprite_atlas.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>

namespace i=wa2::image;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
template<class F>void rejects(F&& f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}CHECK(caught);}
std::shared_ptr<const i::Raster> image(unsigned w,unsigned h,unsigned seed){auto value=std::make_shared<i::Raster>();value->width=w;value->height=h;value->rgba.resize(w*h*4);
    for(unsigned n=0;n<value->rgba.size();++n)value->rgba[n]=std::uint8_t(seed+n*17);return value;}
std::array<std::uint8_t,4> pixel(const i::Raster& image,unsigned x,unsigned y){std::array<std::uint8_t,4> rgba;std::copy_n(image.rgba.data()+(y*image.width+x)*4,4,rgba.data());return rgba;}
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{},guards{};const std::array<std::shared_ptr<const i::Raster>,3> sources{image(2,2,1),image(2,1,47),image(1,3,73)};
    const auto original=sources[0]->rgba;const auto atlas=i::make_sprite_atlas(sources,16);CHECK(atlas && atlas->image->width==11 && atlas->image->height==5 && atlas->regions.size()==3);++cases;
    for(unsigned n=0;n<sources.size();++n){const auto& region=atlas->regions[n];const auto& source=*sources[n];CHECK(region.width==source.width && region.height==source.height);
        for(unsigned y=0;y<source.height+2;++y)for(unsigned x=0;x<source.width+2;++x)CHECK(pixel(*atlas->image,region.x+x-1,region.y+y-1)==pixel(source,std::clamp<int>(int(x)-1,0,int(source.width)-1),std::clamp<int>(int(y)-1,0,int(source.height)-1)));++cases;}
    CHECK(sources[0]->rgba==original);++cases;
    const auto shelves=i::make_sprite_atlas(sources,9);CHECK(shelves && shelves->image->width==8 && shelves->image->height==9 && shelves->regions[2].y==5);++cases;
    CHECK(!i::make_sprite_atlas(sources,8) && !i::make_sprite_atlas(sources,3));++cases;
    std::array<std::shared_ptr<const i::Raster>,3> duplicate{sources[0],sources[1],sources[0]};const auto compact=i::make_sprite_atlas(duplicate,16);CHECK(compact && compact->image->width==8 && compact->regions[0].x==compact->regions[2].x && compact->regions[0].y==compact->regions[2].y);++cases;
    duplicate.fill(sources[0]);const auto alias=i::make_sprite_atlas(duplicate);CHECK(alias && alias->image==sources[0] && alias->regions[2].x==0);CHECK(!i::make_sprite_atlas(duplicate,1));++cases;
    rejects([&]{i::make_sprite_atlas({},16);});rejects([&]{i::make_sprite_atlas(sources,0);});rejects([&]{i::make_sprite_atlas(sources,4097);});guards+=3;
    duplicate[0]={};rejects([&]{i::make_sprite_atlas(duplicate);});++guards;
    duplicate[0]=std::make_shared<i::Raster>(i::Raster{2,2,{0}});rejects([&]{i::make_sprite_atlas(duplicate);});++guards;
    std::ofstream report(argv[1]);report<<"{\"schema\":\"wa2.native-sprite-atlas.v1\",\"cases\":"<<cases<<",\"guards\":"<<guards<<",\"replicated_gutters_verified\":true,\"input_pixels_unchanged\":true,\"synthetic_only\":true,\"devices_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";
    std::cout<<"PASS native sprite atlas pixels, gutters and packing: "<<cases<<" cases.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
