#include "pixel_transfer.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <vector>
namespace wa2::image {namespace {
std::size_t extent(std::size_t pitch,std::size_t row,std::uint32_t height){
    constexpr std::size_t Limit=64u*1024*1024;
    if(pitch<row || pitch>Limit || row>Limit || height>Limit/pitch)throw std::invalid_argument("pixel rectangle exceeds native transfer bounds");
    return pitch*(height-1)+row;
}
template<bool Swap,bool Opaque> void rows(std::uint8_t* output,std::size_t dp,const std::uint8_t* input,std::size_t sp,std::uint32_t width,std::uint32_t height){
    for(std::uint32_t y=0;y<height;++y){auto* d=output+std::size_t(y)*dp;const auto* s=input+std::size_t(y)*sp;
        if constexpr(!Swap && !Opaque)std::memmove(d,s,std::size_t(width)*4);
        else for(std::uint32_t x=0;x<width;++x){const auto at=std::size_t(x)*4;
            const auto r=s[at],g=s[at+1],b=s[at+2],a=s[at+3];
            d[at]=Swap?b:r;d[at+1]=g;d[at+2]=Swap?r:b;d[at+3]=Opaque?255:a;
        }
    }
}
template<bool Bgra> void bgr24_rows(std::uint8_t* output,std::size_t dp,const std::uint8_t* input,std::size_t sp,std::uint32_t width,std::uint32_t height){
    for(std::uint32_t y=0;y<height;++y){auto* d=output+std::size_t(y)*dp;const auto* s=input+std::size_t(y)*sp;
        for(std::uint32_t x=0;x<width;++x){const auto in=std::size_t(x)*3,out=std::size_t(x)*4;
            d[out]=s[in+(Bgra?0:2)];d[out+1]=s[in+1];d[out+2]=s[in+(Bgra?2:0)];d[out+3]=255;}
    }
}
}
void transfer_32(std::span<std::uint8_t> destination,std::size_t destination_pitch,PixelOrder destination_order,
    std::span<const std::uint8_t> source,std::size_t source_pitch,PixelOrder source_order,std::uint32_t width,std::uint32_t height){
    if(unsigned(source_order)>3 || unsigned(destination_order)>3)throw std::invalid_argument("unknown native pixel order");
    if(!width || !height)return;
    const auto row=std::size_t(width)*4,src_size=extent(source_pitch,row,height),dst_size=extent(destination_pitch,row,height);
    if(source.size()<src_size || destination.size()<dst_size)throw std::invalid_argument("truncated native pixel rectangle");
    std::vector<std::uint8_t> saved;
    const auto src=reinterpret_cast<std::uintptr_t>(source.data()),dst=reinterpret_cast<std::uintptr_t>(destination.data());
    if(src<dst+dst_size && dst<src+src_size && (src!=dst || source_pitch!=destination_pitch)){
        saved.assign(source.begin(),source.begin()+src_size);source=saved;
    }
    const bool swap=(unsigned(source_order)&1)!=(unsigned(destination_order)&1),opaque=(unsigned(source_order)|unsigned(destination_order))&2;
    if(swap){if(opaque)rows<true,true>(destination.data(),destination_pitch,source.data(),source_pitch,width,height);
        else rows<true,false>(destination.data(),destination_pitch,source.data(),source_pitch,width,height);}
    else if(opaque)rows<false,true>(destination.data(),destination_pitch,source.data(),source_pitch,width,height);
    else rows<false,false>(destination.data(),destination_pitch,source.data(),source_pitch,width,height);
}
void transfer_bgr24(std::span<std::uint8_t> destination,std::size_t destination_pitch,PixelOrder destination_order,
    std::span<const std::uint8_t> source,std::size_t source_pitch,std::uint32_t width,std::uint32_t height){
    if(unsigned(destination_order)>3)throw std::invalid_argument("unknown native pixel order");
    if(!width || !height)return;
    const auto src_size=extent(source_pitch,std::size_t(width)*3,height),dst_size=extent(destination_pitch,std::size_t(width)*4,height);
    if(source.size()<src_size || destination.size()<dst_size)throw std::invalid_argument("truncated native pixel rectangle");
    std::vector<std::uint8_t> saved;
    const auto src=reinterpret_cast<std::uintptr_t>(source.data()),dst=reinterpret_cast<std::uintptr_t>(destination.data());
    if(src<dst+dst_size && dst<src+src_size){saved.assign(source.begin(),source.begin()+src_size);source=saved;}
    if(unsigned(destination_order)&1)bgr24_rows<true>(destination.data(),destination_pitch,source.data(),source_pitch,width,height);
    else bgr24_rows<false>(destination.data(),destination_pitch,source.data(),source_pitch,width,height);
}
}
