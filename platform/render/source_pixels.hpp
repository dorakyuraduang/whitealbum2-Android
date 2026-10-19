#pragma once
#include <winrecomp/memory.hpp>
#include <span>
#include <stdexcept>
#include <vector>
#include "pixel_transfer.hpp"
namespace wa2::graphics {
// Copy into native image storage without an intermediate whole-image vector.
// Validate the complete source first so a late page fault cannot leave a
// partially modified texture. Padding is readable but never copied to pixels.
inline void read_source_pixel_rows(wr::Memory& memory,std::uint32_t source,std::size_t source_pitch,
    std::span<std::uint8_t> destination,std::size_t destination_pitch,std::size_t row_bytes,std::uint32_t rows){
    constexpr std::size_t Limit=64u*1024*1024;
    if(!row_bytes || !rows || source_pitch<row_bytes || destination_pitch<row_bytes ||
       source_pitch>Limit || destination_pitch>Limit || rows>Limit/source_pitch || rows>Limit/destination_pitch ||
       destination_pitch*(rows-1)+row_bytes>destination.size())throw std::invalid_argument("invalid source pixel rectangle");
    auto scope=memory.execution_scope();memory.check(source,source_pitch*rows,wr::Memory::Read);
    if(source_pitch==row_bytes && destination_pitch==row_bytes)memory.copy_out(source,destination.first(row_bytes*rows));
    else for(std::uint32_t y=0;y<rows;++y)memory.copy_out(source+std::uint32_t(y*source_pitch),destination.subspan(y*destination_pitch,row_bytes));
}

// Common unscaled image upload. Admit only bounded 24/32-bit rectangles and
// validate every accessed source row before changing native image storage.
// The linear host reads the original allocation directly. Paged memory needs
// one row of temporary storage, never a second full image.
inline bool transfer_source_pixels(wr::Memory& memory,std::uint32_t source,std::size_t source_pitch,unsigned source_bytes,
    wa2::image::PixelOrder source_order,std::span<std::uint8_t> destination,std::size_t destination_pitch,
    wa2::image::PixelOrder destination_order,std::uint32_t width,std::uint32_t height){
    constexpr std::size_t Limit=64u*1024*1024;
    if((source_bytes!=3 && source_bytes!=4) || unsigned(source_order)>3 || unsigned(destination_order)>3 || !width || !height)return false;
    const auto row=std::uint64_t(width)*source_bytes,drow=std::uint64_t(width)*4;
    if(row>Limit || drow>Limit || source_pitch<row || destination_pitch<drow || source_pitch>Limit || destination_pitch>Limit ||
       height>Limit/source_pitch || height>Limit/destination_pitch)return false;
    const auto source_extent=source_pitch*(height-1)+std::size_t(row),destination_extent=destination_pitch*(height-1)+std::size_t(drow);
    if(destination_extent>destination.size() || std::uint64_t(source)+source_extent>0x100000000ull)return false;
    auto scope=memory.execution_scope();const auto* base=scope.linear_base();
    if(!base && row>256u*1024)return false;
    if(source_pitch==row)memory.check(source,source_extent,wr::Memory::Read);
    else for(std::uint32_t y=0;y<height;++y)memory.check(source+std::uint32_t(y*source_pitch),std::size_t(row),wr::Memory::Read);
    const auto convert=[&](std::span<std::uint8_t> dst,std::span<const std::uint8_t> src,std::size_t pitch,std::uint32_t rows){
        if(source_bytes==3)wa2::image::transfer_bgr24(dst,destination_pitch,destination_order,src,pitch,width,rows);
        else wa2::image::transfer_32(dst,destination_pitch,destination_order,src,pitch,source_order,width,rows);
    };
    if(base)convert(destination,{base+source,source_extent},source_pitch,height);
    else {auto scratch=std::vector<std::uint8_t>(std::size_t(row));
        for(std::uint32_t y=0;y<height;++y){memory.copy_out(source+std::uint32_t(y*source_pitch),scratch);
            convert(destination.subspan(y*destination_pitch),scratch,std::size_t(row),1);}}
    return true;
}
}
