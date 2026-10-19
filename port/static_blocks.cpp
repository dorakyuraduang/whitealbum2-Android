// Static direct control flow over the identical generated integer bodies.
// Immutable page proofs replace repeated byte checks. Any executable write
// ends the current batch before another instruction can use that proof.
#include <winrecomp/compiled.hpp>
#include "static_blocks.hpp"
#include "fp_sequences.hpp"
#include "block_flags.hpp"
namespace wr {
namespace {
using detail::BlockFlags;
#include "static_blocks_408.inc"
#include "static_blocks_439.inc"
#include "static_blocks_43b.inc"
#include "static_blocks_43c.inc"
#include "static_blocks_43d.inc"
#include "static_blocks_456.inc"
#include "static_blocks_464.inc"
#include "static_blocks_469.inc"
#include "static_blocks_46e.inc"
#include "static_blocks_470.inc"
#include "static_blocks_473.inc"
#include "static_blocks_474.inc"
#include "static_blocks_47e.inc"
#include "static_blocks_49f.inc"
constexpr Wa2BlockEntry entries[]={
#include "static_blocks_entries.inc"
};
}
unsigned wa2_block_batch(Cpu& s,Memory& m,std::uint64_t& budget,unsigned limit,U32 return_pc) {
    switch(s.eip>>12){
    case 0x408:return block_page_1032(s,m,budget,limit,return_pc);
    case 0x439:return block_page_1081(s,m,budget,limit,return_pc);
    case 0x43b:return block_page_1083(s,m,budget,limit,return_pc);
    case 0x43c:return block_page_1084(s,m,budget,limit,return_pc);
    case 0x43d:return block_page_1085(s,m,budget,limit,return_pc);
    case 0x456:return block_page_1110(s,m,budget,limit,return_pc);
    case 0x464:return block_page_1124(s,m,budget,limit,return_pc);
    case 0x469:return block_page_1129(s,m,budget,limit,return_pc);
    case 0x46e:return block_page_1134(s,m,budget,limit,return_pc);
    case 0x470:return block_page_1136(s,m,budget,limit,return_pc);
    case 0x473:return block_page_1139(s,m,budget,limit,return_pc);
    case 0x474:return block_page_1140(s,m,budget,limit,return_pc);
    case 0x47e:return block_page_1150(s,m,budget,limit,return_pc);
    case 0x49f:return block_page_1183(s,m,budget,limit,return_pc);
    default:return 0;
    }
}
std::span<const Wa2BlockEntry> wa2_block_entries(){return entries;}
bool wa2_block_code_verified(Memory& m,U32 pc){
    switch(pc>>12){
    case 0x408:return m.matches_static_code_page<page_signature_1032>(0x408000);
    case 0x439:return m.matches_static_code_page<page_signature_1081>(0x439000);
    case 0x43b:return m.matches_static_code_page<page_signature_1083>(0x43b000);
    case 0x43c:return m.matches_static_code_page<page_signature_1084>(0x43c000);
    case 0x43d:return m.matches_static_code_page<page_signature_1085>(0x43d000);
    case 0x456:return m.matches_static_code_page<page_signature_1110>(0x456000);
    case 0x464:return m.matches_static_code_page<page_signature_1124>(0x464000);
    case 0x469:return m.matches_static_code_page<page_signature_1129>(0x469000);
    case 0x46e:return m.matches_static_code_page<page_signature_1134>(0x46e000);
    case 0x470:return m.matches_static_code_page<page_signature_1136>(0x470000);
    case 0x473:return m.matches_static_code_page<page_signature_1139>(0x473000);
    case 0x474:return m.matches_static_code_page<page_signature_1140>(0x474000);
    case 0x47e:return m.matches_static_code_page<page_signature_1150>(0x47e000);
    case 0x49f:return m.matches_static_code_page<page_signature_1183>(0x49f000);
    default:return false;
    }
}
}
