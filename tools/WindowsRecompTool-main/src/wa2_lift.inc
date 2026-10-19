// Included in lift.cpp after the standard operand readers/writers.
bool xmm_operand(const ZydisDecodedOperand& op) {
    return op.type==ZYDIS_OPERAND_TYPE_REGISTER && op.reg.value>=ZYDIS_REGISTER_XMM0 && op.reg.value<=ZYDIS_REGISTER_XMM7;
}
unsigned xmm_number(const ZydisDecodedOperand& op) {
    if(!xmm_operand(op))throw std::runtime_error("expected XMM0-XMM7");
    return unsigned(op.reg.value-ZYDIS_REGISTER_XMM0);
}
std::optional<std::string> translate_wa2_sse(const Instruction& i) {
    const auto op=i.decoded.mnemonic;const auto& d=i.operands[0];const auto& src=i.operands[1];
    const auto dst_index=xmm_operand(d)?std::to_string(xmm_number(d)):std::string{};
    if(op==ZYDIS_MNEMONIC_MOVD) {
        if(xmm_operand(d) && src.size==32)return "wr::xmm_from_dword(s,"+dst_index+","+read(i,src)+");";
        if(xmm_operand(src) && d.size==32)return write(i,d,"wr::xmm_dword(s,"+std::to_string(xmm_number(src))+")");
        throw std::runtime_error("unsupported MOVD operands");
    }
    if(op==ZYDIS_MNEMONIC_PEXTRW) {
        if(!xmm_operand(src) || d.size!=32)throw std::runtime_error("unsupported PEXTRW operands");
        return write(i,d,"wr::xmm_word(s,"+std::to_string(xmm_number(src))+","+number(std::uint32_t(i.operands[2].imm.value.u))+")");
    }
    if(op==ZYDIS_MNEMONIC_PINSRW) {
        if(!xmm_operand(d) || (src.size!=16 && src.size!=32))throw std::runtime_error("unsupported PINSRW operands");
        return "wr::xmm_insert_word(s,"+dst_index+","+read(i,src)+","+number(std::uint32_t(i.operands[2].imm.value.u))+");";
    }
    if(op==ZYDIS_MNEMONIC_PMOVMSKB) {
        if(!xmm_operand(src) || d.size!=32)throw std::runtime_error("unsupported PMOVMSKB operands");
        return write(i,d,"wr::xmm_byte_mask(s,"+std::to_string(xmm_number(src))+")");
    }
    if(op==ZYDIS_MNEMONIC_SHUFPD) {
        if(!xmm_operand(d))throw std::runtime_error("unsupported SHUFPD destination");
        const auto immediate=number(std::uint32_t(i.operands[2].imm.value.u));
        if(xmm_operand(src))return "wr::xmm_shuffle_pd_reg(s,"+dst_index+","+std::to_string(xmm_number(src))+","+immediate+");";
        if(src.type==ZYDIS_OPERAND_TYPE_MEMORY && src.size==128)return "wr::xmm_shuffle_pd_memory(s,m,"+dst_index+","+address(i,src)+","+immediate+");";
        throw std::runtime_error("unsupported SHUFPD source");
    }
    if(op==ZYDIS_MNEMONIC_PSHUFD) {
        if(!xmm_operand(d))throw std::runtime_error("unsupported PSHUFD destination");
        const auto immediate=number(std::uint32_t(i.operands[2].imm.value.u));
        if(xmm_operand(src))return "wr::xmm_shuffle_reg(s,"+dst_index+","+std::to_string(xmm_number(src))+","+immediate+");";
        if(src.type==ZYDIS_OPERAND_TYPE_MEMORY && src.size==128)return "wr::xmm_shuffle_memory(s,m,"+dst_index+","+address(i,src)+","+immediate+");";
        throw std::runtime_error("unsupported PSHUFD source");
    }
    if((op==ZYDIS_MNEMONIC_PSLLQ || op==ZYDIS_MNEMONIC_PSRLQ) && src.type==ZYDIS_OPERAND_TYPE_IMMEDIATE) {
        if(!xmm_operand(d))throw std::runtime_error("unsupported packed shift destination");
        return "wr::xmm_shift_qwords(s,"+dst_index+","+number(std::uint32_t(src.imm.value.u))+","+(op==ZYDIS_MNEMONIC_PSLLQ?"true":"false")+");";
    }
    if(op==ZYDIS_MNEMONIC_CVTSI2SD) {
        if(!xmm_operand(d) || src.size!=32)throw std::runtime_error("unsupported CVTSI2SD operands");
        if(src.type==ZYDIS_OPERAND_TYPE_MEMORY)return "wr::sse_memory(s,m,"+sse_native_name(i)+","+address(i,src)+",4,false,0);";
        return "wr::sse_i32_to_sd(s,"+sse_native_name(i)+","+read(i,src)+");";
    }
    if(op==ZYDIS_MNEMONIC_CVTSD2SI || op==ZYDIS_MNEMONIC_CVTTSD2SI) {
        if(d.type!=ZYDIS_OPERAND_TYPE_REGISTER || d.size!=32)throw std::runtime_error("unsupported scalar conversion destination");
        if(src.type==ZYDIS_OPERAND_TYPE_MEMORY && src.size==64)return write(i,d,"wr::sse_sd_memory_to_i32(s,m,"+sse_native_name(i)+","+address(i,src)+")");
        if(xmm_operand(src))return write(i,d,"wr::sse_sd_to_i32(s,"+sse_native_name(i)+")");
        throw std::runtime_error("unsupported scalar conversion source");
    }
    return {};
}
