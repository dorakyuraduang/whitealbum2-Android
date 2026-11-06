#include "sprite_atlas.hpp"
#include <algorithm>

namespace wa2::image {
std::optional<SpriteAtlas> make_sprite_atlas(std::span<const std::shared_ptr<const Raster>> images,std::uint32_t limit){
    if(images.empty() || images.size()>16 || limit<1 || limit>4096)throw Error("Invalid native sprite atlas dimensions");
    std::vector<std::shared_ptr<const Raster>> unique;std::vector<unsigned> indices;
    for(const auto& image:images){
        if(!image || !image->width || !image->height || image->width>65535 || image->height>65535 || std::uint64_t(image->width)*image->height*4!=image->rgba.size())throw Error("Invalid native sprite atlas source");
        const auto found=std::find(unique.begin(),unique.end(),image);const auto index=unsigned(found-unique.begin());if(found==unique.end())unique.push_back(image);indices.push_back(index);
    }
    if(unique.size()==1){if(unique[0]->width>limit || unique[0]->height>limit)return {};return SpriteAtlas{unique[0],std::vector<AtlasRegion>(images.size(),{0,0,unique[0]->width,unique[0]->height})};}
    std::vector<AtlasRegion> regions(unique.size());std::uint32_t width{},height{};bool packed=false;
    // Try one horizontal row, one vertical column, then bounded shelves.
    for(unsigned mode=0;mode<3 && !packed;++mode){std::uint32_t x{},y{},row{};width=height=0;bool fits=true;
        for(unsigned n=0;n<unique.size();++n){const auto& image=*unique[n];
            if(image.width>limit || image.height>limit || image.width+2>limit || image.height+2>limit){fits=false;break;}
            const auto w=image.width+2,h=image.height+2;
            if(mode==1){x=0;if(n)y+=row;row=h;}
            else if(mode==2 && x+w>limit){x=0;y+=row;row=0;}
            if(x+w>limit || y+h>limit){fits=false;break;}
            regions[n]={x+1,y+1,image.width,image.height};width=std::max(width,x+w);height=std::max(height,y+h);
            if(mode!=1){x+=w;row=std::max(row,h);}
        }
        packed=fits;
    }
    if(!packed)return {};
    auto output=std::make_shared<Raster>();output->width=width;output->height=height;output->rgba.resize(std::size_t(width)*height*4);
    for(unsigned n=0;n<unique.size();++n){const auto& image=*unique[n];const auto& region=regions[n];
        for(std::uint32_t y=0;y<image.height+2;++y){const auto source_y=std::clamp<int>(int(y)-1,0,int(image.height)-1);auto* row=output->rgba.data()+((std::size_t(region.y+y-1)*width)+region.x-1)*4;
            const auto* source=image.rgba.data()+std::size_t(source_y)*image.width*4;
            std::copy_n(source,4,row);std::copy_n(source,image.width*4,row+4);std::copy_n(source+(image.width-1)*4,4,row+(image.width+1)*4);
        }
    }
    SpriteAtlas result{std::move(output),{}};result.regions.reserve(images.size());for(auto index:indices)result.regions.push_back(regions[index]);return result;
}
}
