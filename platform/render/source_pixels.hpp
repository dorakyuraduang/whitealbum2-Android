#pragma once
#include <winrecomp/memory.hpp>
#include <span>
#include <stdexcept>
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
}
