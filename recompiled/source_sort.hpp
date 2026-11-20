#pragma once
#include <winrecomp/runtime.hpp>
namespace wa2::aot {
struct SortSource {
    wr::U32 begin,end,objects,mask;
    unsigned frame_register;
    std::int32_t pointer,keys,ids,first,last;
};
inline constexpr SortSource main_sort_source{0x442290,0x442374,0xab3fb9,0x4c22b0,
    wr::ESP,0x1c,0x438,0x1438,0x1c,0x2438};
inline thread_local bool source_sort_reference{};
struct SourceSortReferenceScope {
    bool previous{source_sort_reference};
    SourceSortReferenceScope(){source_sort_reference=true;}
    ~SourceSortReferenceScope(){source_sort_reference=previous;}
};
struct SourceSortCounts {std::uint64_t completed{},objects{},moves{},declined{};};
SourceSortCounts source_sort_counts();
// The generated caller proves its immutable complete source body first.
// Decline commits neither CPU nor memory; exact original AOT remains available.
bool source_sort_full(wr::Cpu&,wr::Memory&,const SortSource&);
}
