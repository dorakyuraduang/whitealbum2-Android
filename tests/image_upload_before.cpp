// CPU-only reference for the previous upload sequence: stage all cropped rows,
// then perform the old per-pixel decode/encode. Compiled at the renderer's O2.
// This does not model disk, GPU upload, readback or whole-game loading latency.
#include "../platform/render/source_pixels.hpp"
#include <algorithm>
#include <array>
#include <vector>
void image_upload_before(wr::Memory& memory,std::uint32_t source,std::size_t source_pitch,unsigned source_bytes,
    wa2::image::PixelOrder source_order,std::span<std::uint8_t> destination,std::size_t dp,
    wa2::image::PixelOrder destination_order,std::uint32_t width,std::uint32_t height){
    const auto pitch=std::size_t(width)*source_bytes;std::vector<std::uint8_t> data(pitch*height);
    if(pitch==source_pitch)memory.copy_out(source,data);
    else for(std::uint32_t y=0;y<height;++y)memory.copy_out(source+std::uint32_t(y*source_pitch),std::span(data).subspan(y*pitch,pitch));
    const bool source_bgra=unsigned(source_order)&1,destination_bgra=unsigned(destination_order)&1;
    const bool source_alpha=!(unsigned(source_order)&2),destination_alpha=!(unsigned(destination_order)&2);
    for(std::uint32_t y=0;y<height;++y){const auto* input=data.data()+y*pitch;auto* output=destination.data()+y*dp;
        for(std::uint32_t x=0;x<width;++x){const auto* pixel=input+x*source_bytes;auto* target=output+x*4;
            const auto red=pixel[source_bytes==3 || source_bgra?2:0],green=pixel[1],blue=pixel[source_bytes==3 || source_bgra?0:2];
            const auto alpha=source_bytes==3 || !source_alpha?std::uint8_t(255):pixel[3];
            target[destination_bgra?2:0]=red;target[1]=green;target[destination_bgra?0:2]=blue;target[3]=destination_alpha?alpha:255;
        }
    }
}
