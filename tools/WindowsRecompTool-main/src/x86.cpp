#include "winrecomp/core.hpp"
#include <algorithm>
#include <stdexcept>
namespace wr {
Decoder::Decoder() {
    if (!ZYAN_SUCCESS(ZydisDecoderInit(&decoder_,ZYDIS_MACHINE_MODE_LEGACY_32,ZYDIS_STACK_WIDTH_32)) ||
        !ZYAN_SUCCESS(ZydisFormatterInit(&formatter_,ZYDIS_FORMATTER_STYLE_INTEL))) throw std::runtime_error("Zydis initialization failed");
}
std::optional<Instruction> Decoder::decode(std::span<const std::uint8_t> bytes, Address address) const {
    if(bytes.empty()) return {};
    Instruction i; i.address=address;
    if (!ZYAN_SUCCESS(ZydisDecoderDecodeFull(&decoder_,bytes.data(),std::min<std::size_t>(bytes.size(),15),&i.decoded,i.operands.data()))) return {};
    if (std::uint64_t(address)+i.decoded.length>0x100000000ull) return {};
    i.bytes.assign(bytes.begin(),bytes.begin()+i.decoded.length);
    char text[512]{};
    if (!ZYAN_SUCCESS(ZydisFormatterFormatInstruction(&formatter_,&i.decoded,i.operands.data(),i.decoded.operand_count_visible,text,sizeof(text),address,nullptr))) throw std::runtime_error("Zydis formatting failed");
    i.text=text;
    switch (i.decoded.meta.category) {
    case ZYDIS_CATEGORY_CALL: i.flow=Flow::call;break;
    case ZYDIS_CATEGORY_UNCOND_BR: i.flow=Flow::jump;break;
    case ZYDIS_CATEGORY_COND_BR: i.flow=Flow::conditional;break;
    case ZYDIS_CATEGORY_RET: i.flow=Flow::ret;break;
    case ZYDIS_CATEGORY_INTERRUPT: case ZYDIS_CATEGORY_SYSCALL: case ZYDIS_CATEGORY_SYSRET: i.flow=Flow::stop;break;
    default:break;
    }
    switch(i.decoded.mnemonic) {
    case ZYDIS_MNEMONIC_HLT:case ZYDIS_MNEMONIC_UD0:case ZYDIS_MNEMONIC_UD1:case ZYDIS_MNEMONIC_UD2:i.flow=Flow::stop;break;
    default:break;
    }
    if ((i.flow==Flow::call || i.flow==Flow::jump || i.flow==Flow::conditional) && i.decoded.operand_count_visible) {
        const auto& op=i.operands[0];
        if(op.type==ZYDIS_OPERAND_TYPE_IMMEDIATE && op.imm.is_relative) {
            ZyanU64 target{};
            if(ZYAN_SUCCESS(ZydisCalcAbsoluteAddress(&i.decoded,&op,address,&target))) i.target=Address(target);
        }
        if(op.type==ZYDIS_OPERAND_TYPE_MEMORY && op.mem.base==ZYDIS_REGISTER_NONE && op.mem.index==ZYDIS_REGISTER_NONE &&
            op.mem.segment!=ZYDIS_REGISTER_FS && op.mem.segment!=ZYDIS_REGISTER_GS) {
            i.absolute_slot=Address(op.mem.disp.value);
            if(i.decoded.address_width==16) *i.absolute_slot &= 0xffffu;
        }
    }
    return i;
}
}
