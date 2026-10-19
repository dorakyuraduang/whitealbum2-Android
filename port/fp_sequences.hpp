#pragma once
#include <winrecomp/runtime.hpp>
namespace wr {
struct Wa2FpOperand {
    int base,index;
    unsigned scale;
    U32 displacement;
    unsigned size;
    bool read,write;
};
struct Wa2FpSequenceInfo {
    U32 pc,end;
    unsigned instructions;
    U32 final_fp_pc;
    unsigned final_fop;
    int last_memory;
    std::span<const Wa2FpOperand> operands;
};
inline constexpr U32 wa2_fp_entry_base=0x408000,wa2_fp_entry_end=0x475000;
extern const std::array<std::uint8_t,(wa2_fp_entry_end-wa2_fp_entry_base)/8> wa2_fp_entry_bitmap;
inline bool wa2_fp_sequence_entry(U32 pc) {
    const auto at=pc-wa2_fp_entry_base;
    return at<wa2_fp_entry_end-wa2_fp_entry_base && (wa2_fp_entry_bitmap[at>>3]&(1u<<(at&7)));
}
inline U32 wa2_fp_operand_address(const Cpu& s,const Wa2FpOperand& operand) {
    return operand.displacement+(operand.base<0?0:s.r[operand.base])+(operand.index<0?0:s.r[operand.index]*operand.scale);
}
unsigned wa2_fp_sequence_batch(Cpu&,Memory&,std::uint64_t&,unsigned,U32);
std::span<const Wa2FpSequenceInfo> wa2_fp_sequences();
}
