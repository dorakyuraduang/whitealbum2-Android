#pragma once
#include <winrecomp/runtime.hpp>
namespace wr {
// Executes a prefix of audited integer instructions. A faulting instruction is
// left to the unchanged one-instruction oracle, including its budget charge.
unsigned wa2_block_batch(Cpu&,Memory&,std::uint64_t&,unsigned,U32);
struct Wa2BlockEntry { U32 pc; unsigned length; };
std::span<const Wa2BlockEntry> wa2_block_entries();
bool wa2_block_code_verified(Memory&,U32);
inline bool wa2_block_page(U32 pc) {
    const auto page=pc>>12;
    return page==0x408 || page==0x439 || page==0x43b || page==0x43c || page==0x43d ||
           page==0x456 || page==0x464 || page==0x469 || page==0x46e || page==0x470 ||
           page==0x473 || page==0x474 || page==0x47e || page==0x49f;
}
}
