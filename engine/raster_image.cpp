#include "raster_image.hpp"
#include <algorithm>
#include <bit>
#include <limits>

namespace wa2::image {
namespace {
std::uint32_t u16(std::span<const std::uint8_t> source,std::size_t at){
    return std::uint32_t(source[at])|(std::uint32_t(source[at+1])<<8);
}
void range(std::span<const std::uint8_t> source,std::size_t at,std::size_t size){
    if(at>source.size() || size>source.size()-at)throw Error("truncated game image");
}
}
TgaLayout inspect_game_tga(std::span<const std::uint8_t> source,bool rgba_palette){
    range(source,0,18);
    if(source[2]==9 || source[2]==10)throw Error("RLE image is unsupported by the game format");
    TgaLayout result;result.width=u16(source,12);result.height=u16(source,14);result.bits=source[16];
    result.top_origin=(source[17]&0x20)!=0;result.data_offset=18;
    if(result.bits==8){
        result.palette_entries=u16(source,5);
        if(result.palette_entries>256)throw Error("game image palette exceeds 256 entries");
        const auto entry_bytes=source[7]==24?3u:4u;
        range(source,18,std::size_t(result.palette_entries)*entry_bytes);
        result.alpha=entry_bytes==4;
        for(std::uint32_t i=0;i<result.palette_entries;++i){
            const auto at=18+std::size_t(i)*entry_bytes;
            result.palette[i]={source[at+(rgba_palette?2:0)],source[at+1],source[at+(rgba_palette?0:2)],entry_bytes==4?source[at+3]:std::uint8_t(255)};
        }
        result.data_offset+=result.palette_entries*entry_bytes;
    }else if(result.bits==32)result.alpha=true;
    else if(result.bits!=24)throw Error("unsupported game image bit depth");
    return result;
}
namespace {
Raster decode(std::span<const std::uint8_t> source,const TgaLayout& layout,Region region){
    if(region.x>layout.width || region.y>layout.height || region.width>layout.width-region.x || region.height>layout.height-region.y)
        throw Error("game image region out of bounds");
    const auto pixels=std::uint64_t(region.width)*region.height;
    constexpr std::uint64_t limit=128ull*1024*1024;
    if(pixels>limit/4)throw Error("decoded game image exceeds 128 MiB");
    const auto stride=std::size_t(layout.width)*(layout.bits/8);
    range(source,layout.data_offset,stride*layout.height);
    Raster raster{region.width,region.height,std::vector<std::uint8_t>(std::size_t(pixels)*4)};
    if(!pixels)return raster;
    for(std::uint32_t y=0;y<region.height;++y){
        const auto logical_y=region.y+y;
        const auto source_y=layout.top_origin?logical_y:layout.height-1-logical_y;
        const auto row=source.subspan(layout.data_offset+std::size_t(source_y)*stride+std::size_t(region.x)*(layout.bits/8),std::size_t(region.width)*(layout.bits/8));
        auto* output=raster.rgba.data()+std::size_t(y)*region.width*4;
        for(std::uint32_t x=0;x<region.width;++x,output+=4){
            if(layout.bits==8){
                const auto index=row[x];if(index>=layout.palette_entries)throw Error("game image palette index out of range");
                std::copy(layout.palette[index].begin(),layout.palette[index].end(),output);
            }else{
                const auto at=std::size_t(x)*(layout.bits/8);
                output[0]=row[at+2];output[1]=row[at+1];output[2]=row[at];output[3]=layout.bits==32?row[at+3]:255;
            }
        }
    }
    return raster;
}
}
Raster decode_game_tga(std::span<const std::uint8_t> source){
    const auto layout=inspect_game_tga(source);return decode(source,layout,{0,0,layout.width,layout.height});
}
Raster decode_game_tga_region(std::span<const std::uint8_t> source,Region region){return decode(source,inspect_game_tga(source),region);}
Raster decode_game_bmp(std::span<const std::uint8_t> source){
    range(source,0,54);
    const auto word=[&](std::size_t at){range(source,at,4);return std::uint32_t(source[at])|
        (std::uint32_t(source[at+1])<<8)|(std::uint32_t(source[at+2])<<16)|(std::uint32_t(source[at+3])<<24);};
    if(source[0]!='B' || source[1]!='M' || word(14)<40 || u16(source,26)!=1 || word(30)!=0)
        throw Error("unsupported native BMP header or compression");
    const auto header=word(14),offset=word(10);range(source,14,header);
    const auto width=std::bit_cast<std::int32_t>(word(18)),signed_height=std::bit_cast<std::int32_t>(word(22));
    if(width<=0 || !signed_height || signed_height==std::numeric_limits<std::int32_t>::min())throw Error("invalid native BMP dimensions");
    const auto height=std::uint32_t(signed_height<0?-signed_height:signed_height),bits=u16(source,28);
    if(bits!=8 && bits!=24 && bits!=32)throw Error("unsupported native BMP depth");
    const auto pixels=std::uint64_t(width)*height;
    if(pixels>128ull*1024*1024/4)throw Error("decoded native BMP exceeds 128 MiB");
    const auto stride=(std::uint64_t(width)*bits+31)/32*4;
    if(offset<14ull+header)throw Error("native BMP pixels overlap header");
    range(source,offset,std::size_t(stride*height));
    std::array<std::array<std::uint8_t,4>,256> palette{};auto colours=word(46);
    if(bits==8){
        if(!colours)colours=256;
        if(colours>256 || 14ull+header+colours*4>offset)throw Error("invalid native BMP palette");
        for(std::uint32_t n=0;n<colours;++n){const auto at=14+header+n*4;
            // The original A8 upload uses the palette's blue component.
            palette[n]={source[at+2],source[at+1],source[at],source[at]};}
    }
    Raster result{std::uint32_t(width),height,std::vector<std::uint8_t>(std::size_t(pixels)*4)};
    for(std::uint32_t y=0;y<height;++y){const auto row=offset+std::size_t(signed_height<0?y:height-1-y)*stride;
        for(std::uint32_t x=0;x<std::uint32_t(width);++x){auto* out=result.rgba.data()+(std::size_t(y)*width+x)*4;
            if(bits==8){const auto index=source[row+x];if(index>=colours)throw Error("native BMP palette index out of range");
                std::copy(palette[index].begin(),palette[index].end(),out);
            }else{const auto at=row+std::size_t(x)*(bits/8);
                out[0]=source[at+2];out[1]=source[at+1];out[2]=source[at];out[3]=bits==32?source[at+3]:255;}
        }
    }
    return result;
}
}
