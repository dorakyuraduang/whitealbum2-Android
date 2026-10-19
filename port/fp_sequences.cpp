#include "fp_sequences.hpp"
#include <algorithm>

namespace wr {
namespace {
struct FpSequence {
    Wa2FpSequenceInfo info;
    std::span<const std::uint8_t> code;
    void(*native)(void*,void*);
};
#include "fp_sequences_data.inc"
bool overlaps(std::uint64_t a,std::uint64_t n,std::uint64_t b,std::uint64_t k) {
    return a<b+k && b<a+n;
}
}
const std::array<std::uint8_t,(wa2_fp_entry_end-wa2_fp_entry_base)/8> wa2_fp_entry_bitmap=[] {
    std::array<std::uint8_t,(wa2_fp_entry_end-wa2_fp_entry_base)/8> result{};
    for(const auto& sequence:sequences) {
        const auto offset=sequence.info.pc-wa2_fp_entry_base;result[offset>>3]|=std::uint8_t(1u<<(offset&7));
    }
    return result;
}();
std::span<const Wa2FpSequenceInfo> wa2_fp_sequences() {
    static const auto info=[] {
        std::array<Wa2FpSequenceInfo,std::size(sequences)> result{};
        for(unsigned k=0;k<result.size();++k)result[k]=sequences[k].info;
        return result;
    }();
    return info;
}
unsigned wa2_fp_sequence_batch(Cpu& s,Memory& m,std::uint64_t& budget,unsigned limit,U32 return_pc) {
    if(!wa2_fp_sequence_entry(s.eip) || (s.fp.get16(0)&0x3f)!=0x3f)return 0;
    const auto found=std::lower_bound(std::begin(sequences),std::end(sequences),s.eip,
        [](const FpSequence& sequence,U32 pc){return sequence.info.pc<pc;});
    if(found==std::end(sequences) || found->info.pc!=s.eip)return 0;
    const auto& info=found->info;
    if(limit<info.instructions || budget<info.instructions || (return_pc>=info.pc && return_pc<info.end))return 0;
    alignas(16) std::array<std::uint8_t,max_fp_operands*16> scratch{};
    std::array<U32,max_fp_operands> addresses{};
    auto guard=m.lock();
    try {
        if(!m.matches_code(info.pc,found->code))return 0;
        for(unsigned k=0;k<info.operands.size();++k) {
            const auto& operand=info.operands[k];const auto at=addresses[k]=wa2_fp_operand_address(s,operand);
            if(std::uint64_t(at)+operand.size>0x100000000ull ||
               (operand.write && overlaps(at,operand.size,info.pc,info.end-info.pc)))return 0;
            // Equal symbolic expressions share one slot. Dynamic aliasing of
            // distinct expressions is left to the exact instruction oracle.
            for(unsigned j=0;j<k;++j)if(overlaps(at,operand.size,addresses[j],info.operands[j].size))return 0;
            m.check(at,operand.size,(operand.read?Memory::Read:0)|(operand.write?Memory::Write:0));
            if(operand.read)m.copy_out(at,std::span(scratch).subspan(k*16,operand.size));
        }
    }catch(const GuestFault&){return 0;}
    auto result=s.fp;found->native(result.bytes.data(),scratch.data());
    // x87_execute restores the caller's exact control-word bits, including
    // reserved bits that FXRSTOR/FXSAVE normalize in the hardware image.
    result.put16(0,s.fp.get16(0));
    result.put64(8,info.final_fp_pc);result.put16(6,std::uint16_t(info.final_fop));
    result.put64(16,info.last_memory<0?s.fp.get64(16):addresses[info.last_memory]);
    for(unsigned k=0;k<info.operands.size();++k)if(info.operands[k].write)
        m.copy_in(addresses[k],std::span(scratch).subspan(k*16,info.operands[k].size));
    s.fp=result;s.eip=info.end;budget-=info.instructions;return info.instructions;
}
}
