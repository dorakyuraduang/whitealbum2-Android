#include "winrecomp/core.hpp"
#include "winrecomp/hash.hpp"
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace wr {
namespace {
struct Register { unsigned index,width,shift; };
Register reginfo(ZydisRegister r) {
    switch(r) {
#define R(name,idx,w,s) case ZYDIS_REGISTER_##name:return {idx,w,s};
    R(EAX,0,32,0) R(ECX,1,32,0) R(EDX,2,32,0) R(EBX,3,32,0) R(ESP,4,32,0) R(EBP,5,32,0) R(ESI,6,32,0) R(EDI,7,32,0)
    R(AX,0,16,0) R(CX,1,16,0) R(DX,2,16,0) R(BX,3,16,0) R(SP,4,16,0) R(BP,5,16,0) R(SI,6,16,0) R(DI,7,16,0)
    R(AL,0,8,0) R(CL,1,8,0) R(DL,2,8,0) R(BL,3,8,0) R(AH,0,8,8) R(CH,1,8,8) R(DH,2,8,8) R(BH,3,8,8)
#undef R
    default:throw std::runtime_error("unsupported register");
    }
}
std::string number(std::uint32_t v){return hex(v)+"u";}
std::string readreg(ZydisRegister r,const std::string& state="s") {
    if(r>=ZYDIS_REGISTER_ES && r<=ZYDIS_REGISTER_GS)return state+".selectors.at("+std::to_string(unsigned(r-ZYDIS_REGISTER_ES))+")";
    const auto x=reginfo(r);return "wr::reg("+state+","+std::to_string(x.index)+","+std::to_string(x.width)+","+std::to_string(x.shift)+")";
}
std::string address(const Instruction& i,const ZydisDecodedOperand& op,bool lea=false,const std::string& state="s") {
    if(op.type!=ZYDIS_OPERAND_TYPE_MEMORY)throw std::runtime_error("expected memory operand");
    if(i.decoded.address_width!=16 && i.decoded.address_width!=32)throw std::runtime_error("unsupported address width");
    std::string e=number(std::uint32_t(op.mem.disp.value));
    if(op.mem.base!=ZYDIS_REGISTER_NONE)e+="+"+readreg(op.mem.base,state);
    if(op.mem.index!=ZYDIS_REGISTER_NONE)e+="+"+readreg(op.mem.index,state)+"*"+std::to_string(op.mem.scale)+"u";
    auto result="(("+e+")&"+(i.decoded.address_width==16?"0xffffu":"0xffffffffu")+")";
    if(!lea && (op.mem.segment==ZYDIS_REGISTER_FS || op.mem.segment==ZYDIS_REGISTER_GS))
        result="wr::segment_address("+state+","+result+","+(op.mem.segment==ZYDIS_REGISTER_FS?"1":"2")+")";
    return result;
}
std::string read(const Instruction& i,const ZydisDecodedOperand& op) {
    switch(op.type) {
    case ZYDIS_OPERAND_TYPE_REGISTER:return readreg(op.reg.value);
    case ZYDIS_OPERAND_TYPE_IMMEDIATE:if(op.imm.is_relative)throw std::runtime_error("relative immediate used as data");return number(std::uint32_t(op.imm.value.u));
    case ZYDIS_OPERAND_TYPE_MEMORY:
        if(op.size!=8 && op.size!=16 && op.size!=32)throw std::runtime_error("unsupported memory operand width");
        return "m.load("+address(i,op)+","+std::to_string(op.size)+")";
    default:throw std::runtime_error("unsupported operand");
    }
}
std::string write(const Instruction& i,const ZydisDecodedOperand& op,const std::string& v,const std::string& state="s") {
    if(op.type==ZYDIS_OPERAND_TYPE_REGISTER) {
        const auto x=reginfo(op.reg.value);
        return "wr::set_reg("+state+","+std::to_string(x.index)+","+v+","+std::to_string(x.width)+","+std::to_string(x.shift)+");";
    }
    if(op.type==ZYDIS_OPERAND_TYPE_MEMORY && (op.size==8 || op.size==16 || op.size==32))return "m.store("+address(i,op,false,state)+","+v+","+std::to_string(op.size)+");";
    throw std::runtime_error("unsupported destination");
}
std::string arithmetic_flags(const ZydisDecodedOperand& destination,bool writes=true) {
    if(destination.size!=8 && destination.size!=16 && destination.size!=32)
        throw std::runtime_error("unsupported arithmetic operand width");
    // Operands have already been read before this declaration is emitted.
    // These arithmetic helpers cannot fault for the validated literal width,
    // and a fixed GPR commit cannot fault either. Memory writes still need the
    // temporary Cpu so permission/address faults leave the flags unchanged.
    if(!writes)return "auto& f=s;";
    if(destination.type==ZYDIS_OPERAND_TYPE_REGISTER) {
        const auto r=reginfo(destination.reg.value);
        if(r.width!=destination.size)throw std::runtime_error("inconsistent arithmetic register width");
        return "auto& f=s;";
    }
    return "auto f=s;";
}
std::string fp_native_name(const Instruction& i) {
    const auto modrm=i.bytes.at(i.decoded.raw.modrm.offset);
    return "wr_x87_"+std::to_string(i.decoded.opcode)+(modrm>=0xc0?"_r"+std::to_string(modrm):"_m"+std::to_string((modrm>>3)&7u));
}
std::string translate_fp(const Instruction& i) {
    const auto name=std::string(ZydisMnemonicGetString(i.decoded.mnemonic));const auto& d=i.operands[0];
    if(name=="fwait")return "wr::x87_wait(s);";
    if(name=="fnclex")return "wr::x87_clear(s);";
    if(name=="fninit")return "wr::x87_init(s);";
    if(name=="fnstsw")return write(i,d,"s.fp.get16(2)");
    if(name=="fnstcw")return write(i,d,"s.fp.get16(0)");
    if(name=="fnsave" || name=="frstor" || name=="fnstenv" || name=="fldenv") {
        if(i.decoded.operand_width!=32)throw std::runtime_error("16-bit x87 environments not implemented");
        const bool restore=name=="frstor" || name=="fldenv",registers=name=="fnsave" || name=="frstor";
        return (restore?"wr::x87_wait(s);":"")+std::string("wr::x87_env(s,m,")+address(i,d)+","+(restore?"true":"false")+","+(registers?"true":"false")+");";
    }
    static const std::set<std::string> supported={
        "f2xm1","fabs","fadd","faddp","fchs","fcmovb","fcmovbe","fcmove","fcmovnb","fcmovnbe","fcmovne","fcmovnu","fcmovu",
        "fcom","fcomi","fcomip","fcomp","fcompp","fcos","fdecstp","fdiv","fdivp","fdivr","fdivrp","ffree",
        "fiadd","ficom","ficomp","fidiv","fidivr","fild","fimul","fincstp","fist","fistp","fisub","fisubr",
        "fld","fld1","fldl2e","fldl2t","fldlg2","fldln2","fldpi","fldz","fldcw","fmul","fmulp","fnop",
        "fpatan","fprem","fprem1","fptan","frndint","fscale","fsin","fsincos","fsqrt","fst","fstp","fsub","fsubp","fsubr","fsubrp",
        "ftst","fucom","fucomi","fucomip","fucomp","fucompp","fxam","fxch","fxtract","fyl2x","fyl2xp1"};
    if(!supported.contains(name) || i.decoded.opcode<0xd8 || i.decoded.opcode>0xdf)throw std::runtime_error("unsupported x87 operation");
    const auto opcode=((unsigned(i.decoded.opcode)&7u)<<8)|i.bytes.at(i.decoded.raw.modrm.offset);
    const auto native=fp_native_name(i);
    for(unsigned n=0;n<i.decoded.operand_count_visible;++n)if(i.operands[n].type==ZYDIS_OPERAND_TYPE_MEMORY) {
        const auto& op=i.operands[n];const bool store=(op.actions&ZYDIS_OPERAND_ACTION_MASK_WRITE)!=0;
        return "wr::x87_memory(s,m,"+native+","+address(i,op)+","+std::to_string(op.size/8)+","+(store?"true":"false")+","+std::to_string(opcode)+","+(name=="fldcw"?"true":"false")+");";
    }
    const bool integer_flags=name=="fcomi" || name=="fucomi" || name=="fcomip" || name=="fucomip";
    return "wr::x87_execute(s,"+native+",nullptr,"+std::to_string(opcode)+",false,"+(integer_flags?"true":"false")+");";
}

unsigned sse_prefix(const Instruction& i) {
    unsigned prefix=0;
    for(unsigned n=0;n<i.decoded.raw.prefix_count;++n)if(i.decoded.raw.prefixes[n].type==ZYDIS_PREFIX_TYPE_MANDATORY)prefix=i.decoded.raw.prefixes[n].value;
    return prefix;
}
bool is_sse(const Instruction& i) {
    if(i.decoded.mnemonic==ZYDIS_MNEMONIC_CVTSD2SI || i.decoded.mnemonic==ZYDIS_MNEMONIC_CVTTSD2SI)return true;
    for(unsigned n=0;n<i.decoded.operand_count_visible;++n)if(i.operands[n].type==ZYDIS_OPERAND_TYPE_REGISTER && i.operands[n].reg.value>=ZYDIS_REGISTER_XMM0 && i.operands[n].reg.value<=ZYDIS_REGISTER_XMM7)return true;
    return i.decoded.mnemonic==ZYDIS_MNEMONIC_LDMXCSR || i.decoded.mnemonic==ZYDIS_MNEMONIC_STMXCSR;
}
std::string sse_native_name(const Instruction& i) {
    const unsigned modrm=i.bytes.at(i.decoded.raw.modrm.offset);
    if(i.decoded.mnemonic==ZYDIS_MNEMONIC_CVTSI2SD)return "wr_sse_242_42_m"+std::to_string(unsigned(i.operands[0].reg.value-ZYDIS_REGISTER_XMM0));
    if(i.decoded.mnemonic==ZYDIS_MNEMONIC_CVTSD2SI || i.decoded.mnemonic==ZYDIS_MNEMONIC_CVTTSD2SI)
        return "wr_sse_to_i32_"+std::to_string(i.decoded.opcode)+(i.operands[1].type==ZYDIS_OPERAND_TYPE_MEMORY?"_m":"_r"+std::to_string(unsigned(i.operands[1].reg.value-ZYDIS_REGISTER_XMM0)));
    if(i.decoded.mnemonic==ZYDIS_MNEMONIC_CMPPD || i.decoded.mnemonic==ZYDIS_MNEMONIC_CMPSD)
        return "wr_sse_"+std::to_string(sse_prefix(i))+"_"+std::to_string(i.decoded.opcode)+(modrm>=0xc0?"_r"+std::to_string(modrm-0xc0):"_m"+std::to_string((modrm>>3)&7))+"_i"+std::to_string(unsigned(i.operands[2].imm.value.u)&7u);
    return "wr_sse_"+std::to_string(sse_prefix(i))+"_"+std::to_string(i.decoded.opcode)+(modrm>=0xc0?"_r"+std::to_string(modrm-0xc0):"_m"+std::to_string((modrm>>3)&7));
}
#include "wa2_lift.inc"
std::string translate_sse(const Instruction& i) {
    const auto op=i.decoded.mnemonic;
    if(auto extra=translate_wa2_sse(i))return *extra;
    if(op==ZYDIS_MNEMONIC_STMXCSR)return write(i,i.operands[0],"s.fp.get32(24)");
    if(op==ZYDIS_MNEMONIC_LDMXCSR)return "{auto value="+read(i,i.operands[0])+";if(value&0xffff0000u)throw wr::GuestFault(wr::FaultKind::unsupported,s.eip,\"reserved MXCSR bits\");s.fp.put32(24,value);}";
    if(i.decoded.opcode_map!=ZYDIS_OPCODE_MAP_0F)throw std::runtime_error("SIMD opcode map not supported");
    static const std::set<unsigned> recipes={0x10,0x11,0x28,0x29,0x2e,0x2f,0x51,0x52,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x5b,0x5c,0x5d,0x5e,0x5f,0x6610,0x6611,0x6628,0x6629,0x662e,0x662f,0x6651,0x6654,0x6655,0x6656,0x6657,0x6658,0x6659,0x665a,0x665b,0x665c,0x665d,0x665e,0x665f,0x6660,0x6661,0x6662,0x6663,0x6664,0x6665,0x6666,0x6667,0x6668,0x6669,0x666a,0x666b,0x666c,0x666d,0x666f,0x6674,0x6675,0x6676,0x667f,0x66d1,0x66d2,0x66d3,0x66d4,0x66d5,0x66d8,0x66d9,0x66da,0x66db,0x66dc,0x66dd,0x66de,0x66df,0x66e0,0x66e1,0x66e2,0x66e3,0x66e4,0x66e5,0x66e6,0x66e8,0x66e9,0x66ea,0x66eb,0x66ec,0x66ed,0x66ee,0x66ef,0x66f1,0x66f2,0x66f3,0x66f4,0x66f5,0x66f6,0x66f8,0x66f9,0x66fa,0x66fb,0x66fc,0x66fd,0x66fe,0xf210,0xf211,0xf251,0xf258,0xf259,0xf25a,0xf25c,0xf25d,0xf25e,0xf25f,0xf2e6,0xf310,0xf311,0xf351,0xf352,0xf353,0xf358,0xf359,0xf35a,0xf35b,0xf35c,0xf35d,0xf35e,0xf35f,0xf36f,0xf37f,0xf3e6};
    static const std::set<unsigned> extra_recipes={0x6612,0x6613,0x6614,0x6615,0x66d6,0xf37e,0x66c2,0xf2c2};
    if(!recipes.contains((sse_prefix(i)<<8)|i.decoded.opcode) && !extra_recipes.contains((sse_prefix(i)<<8)|i.decoded.opcode))throw std::runtime_error("SIMD instruction recipe not implemented");
    bool integer_flags=false;const auto name=std::string(ZydisMnemonicGetString(op));
    integer_flags=name=="comiss" || name=="comisd" || name=="ucomiss" || name=="ucomisd";
    for(unsigned n=0;n<i.decoded.operand_count_visible;++n) {
        const auto& operand=i.operands[n];
        if(operand.type==ZYDIS_OPERAND_TYPE_REGISTER && (operand.reg.value<ZYDIS_REGISTER_XMM0 || operand.reg.value>ZYDIS_REGISTER_XMM7))throw std::runtime_error("SSE/GPR transfer not implemented");
        if(operand.type==ZYDIS_OPERAND_TYPE_IMMEDIATE && op!=ZYDIS_MNEMONIC_CMPPD && op!=ZYDIS_MNEMONIC_CMPSD)throw std::runtime_error("SIMD immediate not implemented");
    }
    for(unsigned n=0;n<i.decoded.operand_count_visible;++n)if(i.operands[n].type==ZYDIS_OPERAND_TYPE_MEMORY) {
        const auto& operand=i.operands[n];const bool store=(operand.actions&ZYDIS_OPERAND_ACTION_MASK_WRITE)!=0;
        const unsigned align=operand.size==128 && name!="movups" && name!="movupd" && name!="movdqu"?16u:0u;
        return "wr::sse_memory(s,m,"+sse_native_name(i)+","+address(i,operand)+","+std::to_string(operand.size/8)+","+(store?"true":"false")+","+std::to_string(align)+","+(integer_flags?"true":"false")+");";
    }
    return "wr::sse_execute(s,"+sse_native_name(i)+",nullptr,"+(integer_flags?"true":"false")+");";
}
std::string condition(ZydisMnemonic mnemonic) {
    switch(mnemonic){
    case ZYDIS_MNEMONIC_JO:return "(s.flags&wr::OF)!=0";
    case ZYDIS_MNEMONIC_JNO:return "(s.flags&wr::OF)==0";
    case ZYDIS_MNEMONIC_JB:return "(s.flags&wr::CF)!=0";
    case ZYDIS_MNEMONIC_JNB:return "(s.flags&wr::CF)==0";
    case ZYDIS_MNEMONIC_JZ:return "(s.flags&wr::ZF)!=0";
    case ZYDIS_MNEMONIC_JNZ:return "(s.flags&wr::ZF)==0";
    case ZYDIS_MNEMONIC_JBE:return "(s.flags&(wr::CF|wr::ZF))!=0";
    case ZYDIS_MNEMONIC_JNBE:return "(s.flags&(wr::CF|wr::ZF))==0";
    case ZYDIS_MNEMONIC_JS:return "(s.flags&wr::SF)!=0";
    case ZYDIS_MNEMONIC_JNS:return "(s.flags&wr::SF)==0";
    case ZYDIS_MNEMONIC_JP:return "(s.flags&wr::PF)!=0";
    case ZYDIS_MNEMONIC_JNP:return "(s.flags&wr::PF)==0";
    case ZYDIS_MNEMONIC_JL:return "bool(s.flags&wr::SF)!=bool(s.flags&wr::OF)";
    case ZYDIS_MNEMONIC_JNL:return "bool(s.flags&wr::SF)==bool(s.flags&wr::OF)";
    case ZYDIS_MNEMONIC_JLE:return "(s.flags&wr::ZF)!=0 || bool(s.flags&wr::SF)!=bool(s.flags&wr::OF)";
    case ZYDIS_MNEMONIC_JNLE:return "(s.flags&wr::ZF)==0 && bool(s.flags&wr::SF)==bool(s.flags&wr::OF)";
    case ZYDIS_MNEMONIC_JECXZ:return "s.r[wr::ECX]==0";
    case ZYDIS_MNEMONIC_JCXZ:return "(s.r[wr::ECX]&0xffffu)==0";
    default:throw std::runtime_error("unsupported conditional branch");
    }
}
std::string translate_unlocked(const Instruction& i) {
    if(i.decoded.meta.branch_type==ZYDIS_BRANCH_TYPE_FAR)throw std::runtime_error("far control flow not implemented");
    const auto& d=i.operands[0];const auto& src=i.operands[1];const auto op=i.decoded.mnemonic;
    if((i.decoded.opcode_map==ZYDIS_OPCODE_MAP_DEFAULT && i.decoded.opcode>=0xd8 && i.decoded.opcode<=0xdf) || op==ZYDIS_MNEMONIC_FWAIT)return translate_fp(i);
    if(is_sse(i))return translate_sse(i);
    if(op==ZYDIS_MNEMONIC_CPUID)return "wr::guest_cpuid(s);";
    if(op==ZYDIS_MNEMONIC_RDTSC)return "wr::guest_rdtsc(s);";
    if(op==ZYDIS_MNEMONIC_PUSHFD)return "wr::push(s,m,(s.flags|2u)&~0x30000u);";
    if(op==ZYDIS_MNEMONIC_POPFD)return "wr::guest_popfd(s,m);";
    auto width=std::to_string(d.size);
    // String opcodes are distinguished from SSE MOVSD by instruction category.
    if(i.decoded.meta.category==ZYDIS_CATEGORY_STRINGOP || i.decoded.meta.category==ZYDIS_CATEGORY_IOSTRINGOP) {
        unsigned kind=0,w=8;
        switch(op) {
        case ZYDIS_MNEMONIC_MOVSB:kind=0;w=8;break;case ZYDIS_MNEMONIC_MOVSW:kind=0;w=16;break;case ZYDIS_MNEMONIC_MOVSD:kind=0;w=32;break;
        case ZYDIS_MNEMONIC_STOSB:kind=1;w=8;break;case ZYDIS_MNEMONIC_STOSW:kind=1;w=16;break;case ZYDIS_MNEMONIC_STOSD:kind=1;w=32;break;
        case ZYDIS_MNEMONIC_LODSB:kind=2;w=8;break;case ZYDIS_MNEMONIC_LODSW:kind=2;w=16;break;case ZYDIS_MNEMONIC_LODSD:kind=2;w=32;break;
        case ZYDIS_MNEMONIC_CMPSB:kind=3;w=8;break;case ZYDIS_MNEMONIC_CMPSW:kind=3;w=16;break;case ZYDIS_MNEMONIC_CMPSD:kind=3;w=32;break;
        case ZYDIS_MNEMONIC_SCASB:kind=4;w=8;break;case ZYDIS_MNEMONIC_SCASW:kind=4;w=16;break;case ZYDIS_MNEMONIC_SCASD:kind=4;w=32;break;
        default:throw std::runtime_error("port I/O strings are not supported");
        }
        const unsigned repeat=(i.decoded.attributes&ZYDIS_ATTRIB_HAS_REPNE)?2u:(i.decoded.attributes&(ZYDIS_ATTRIB_HAS_REP|ZYDIS_ATTRIB_HAS_REPE))?1u:0u;
        unsigned segment=0;
        for(unsigned n=0;n<i.decoded.operand_count;++n)if(i.operands[n].type==ZYDIS_OPERAND_TYPE_MEMORY){if(i.operands[n].mem.segment==ZYDIS_REGISTER_FS)segment=1;if(i.operands[n].mem.segment==ZYDIS_REGISTER_GS)segment=2;}
        return "wr::string_op(s,m,"+std::to_string(kind)+","+std::to_string(w)+","+std::to_string(i.decoded.address_width)+","+std::to_string(repeat)+","+std::to_string(segment)+",budget);";
    }
    if(i.decoded.attributes&(ZYDIS_ATTRIB_HAS_REP|ZYDIS_ATTRIB_HAS_REPE|ZYDIS_ATTRIB_HAS_REPNE))throw std::runtime_error("unsupported repeated instruction");
    if(i.flow==Flow::conditional) {
        if(!i.target)throw std::runtime_error("conditional branch without a target");
        if(op==ZYDIS_MNEMONIC_LOOP || op==ZYDIS_MNEMONIC_LOOPE || op==ZYDIS_MNEMONIC_LOOPNE) {
            const auto aw=std::to_string(i.decoded.address_width);
            const std::string extra=op==ZYDIS_MNEMONIC_LOOPE?" && (s.flags&wr::ZF)":op==ZYDIS_MNEMONIC_LOOPNE?" && !(s.flags&wr::ZF)":"";
            return "wr::set_reg(s,wr::ECX,wr::reg(s,wr::ECX,"+aw+")-1,"+aw+");s.eip=(wr::reg(s,wr::ECX,"+aw+")!=0"+extra+")?"+number(*i.target)+":"+number(i.next())+";";
        }
        return "s.eip=("+condition(op)+")?"+number(*i.target)+":"+number(i.next())+";";
    }
    if(op==ZYDIS_MNEMONIC_JMP || op==ZYDIS_MNEMONIC_CALL) {
        if(i.decoded.operand_width!=32)throw std::runtime_error("16-bit control transfer not implemented");
        const auto target=i.target?number(*i.target):read(i,d);
        return "{auto target="+target+";"+(op==ZYDIS_MNEMONIC_CALL?"wr::push(s,m,"+number(i.next())+"); ":"")+"s.eip=target;}";
    }
    if(op==ZYDIS_MNEMONIC_RET) {
        if(i.decoded.operand_width!=32)throw std::runtime_error("16-bit return not implemented");
        auto result=std::string("s.eip=wr::pop(s,m);");
        if(i.decoded.operand_count_visible)result+="s.r[wr::ESP]+="+number(std::uint32_t(d.imm.value.u))+";";
        return result;
    }
    if(op==ZYDIS_MNEMONIC_NOP)return ";";
    if(op==ZYDIS_MNEMONIC_MOV || op==ZYDIS_MNEMONIC_MOVZX || op==ZYDIS_MNEMONIC_MOVSX) {
        auto value=read(i,src);
        if(op==ZYDIS_MNEMONIC_MOVSX)value="wr::sign_extend("+value+","+std::to_string(src.size)+")";
        return "{auto value="+value+";"+write(i,d,"value")+"}";
    }
    if(op==ZYDIS_MNEMONIC_LEA)return write(i,d,address(i,src,true));
    if(op==ZYDIS_MNEMONIC_PUSH) {
        if(i.decoded.operand_width!=16 && i.decoded.operand_width!=32)throw std::runtime_error("unsupported push width");
        return "{auto value="+read(i,d)+";wr::push(s,m,value,"+std::to_string(i.decoded.operand_width)+");}";
    }
    if(op==ZYDIS_MNEMONIC_POP) {
        if(d.type==ZYDIS_OPERAND_TYPE_REGISTER) {
            const auto r=reginfo(d.reg.value);
            if((r.width!=16 && r.width!=32) || r.width!=i.decoded.operand_width || r.width!=d.size)
                throw std::runtime_error("unsupported POP register width");
            // pop performs its load before advancing ESP; the fixed register
            // assignment (including POP ESP/SP) cannot introduce another fault.
            return "{auto value=wr::pop(s,m,"+std::to_string(i.decoded.operand_width)+");"+write(i,d,"value")+"}";
        }
        return "{auto next=s;auto value=wr::pop(next,m,"+std::to_string(i.decoded.operand_width)+");"+write(i,d,"value","next")+"s=next;}";
    }
    if(op==ZYDIS_MNEMONIC_LEAVE) {
        if(i.decoded.operand_width!=32)throw std::runtime_error("16-bit LEAVE not implemented");
        return "{auto p=s.r[wr::EBP];auto v=m.load(p,32);s.r[wr::ESP]=p+4;s.r[wr::EBP]=v;}";
    }
    if(i.decoded.meta.category==ZYDIS_CATEGORY_SETCC)return write(i,d,"wr::test_condition(s,"+std::to_string(i.decoded.opcode&15u)+")?1u:0u");
    if(i.decoded.meta.category==ZYDIS_CATEGORY_CMOV) {
        // CMOV's source load occurs even when the condition is false.
        return "{auto value="+read(i,src)+";if(wr::test_condition(s,"+std::to_string(i.decoded.opcode&15u)+")){"+write(i,d,"value")+"}}";
    }
    switch(op) {
    case ZYDIS_MNEMONIC_CDQ:return "s.r[wr::EDX]=(s.r[wr::EAX]&0x80000000u)?0xffffffffu:0u;";
    case ZYDIS_MNEMONIC_CWD:return "wr::set_reg(s,wr::EDX,(s.r[wr::EAX]&0x8000u)?0xffffu:0u,16);";
    case ZYDIS_MNEMONIC_CWDE:return "s.r[wr::EAX]=wr::sign_extend(s.r[wr::EAX],16);";
    case ZYDIS_MNEMONIC_CBW:return "wr::set_reg(s,wr::EAX,wr::sign_extend(s.r[wr::EAX],8),16);";
    case ZYDIS_MNEMONIC_CLD:return "s.flags&=~wr::DF;";
    case ZYDIS_MNEMONIC_STD:return "s.flags|=wr::DF;";
    case ZYDIS_MNEMONIC_LAHF:return "wr::set_reg(s,wr::EAX,(s.flags&0xd5u)|2u,8,8);";
    case ZYDIS_MNEMONIC_SAHF:return "s.flags=(s.flags&~0xd5u)|(wr::reg(s,wr::EAX,8,8)&0xd5u);s.defined_flags|=0xd5u;";
    case ZYDIS_MNEMONIC_INT3:return "throw wr::GuestFault(wr::FaultKind::breakpoint,s.eip,\"guest breakpoint\");";
    default:break;
    }
    if(op==ZYDIS_MNEMONIC_BSWAP){if(d.size!=32)throw std::runtime_error("undefined 16-bit BSWAP");return "{auto a="+read(i,d)+";auto v=(a>>24)|((a>>8)&0xff00u)|((a<<8)&0xff0000u)|(a<<24);"+write(i,d,"v")+"}";}
    if(op==ZYDIS_MNEMONIC_XLAT) {
        auto off=i.decoded.address_width==16?"((wr::reg(s,wr::EBX,16)+wr::reg(s,wr::EAX,8))&0xffffu)":"(s.r[wr::EBX]+wr::reg(s,wr::EAX,8))";
        unsigned segment=0;for(unsigned n=0;n<i.decoded.operand_count;++n)if(i.operands[n].type==ZYDIS_OPERAND_TYPE_MEMORY){if(i.operands[n].mem.segment==ZYDIS_REGISTER_FS)segment=1;if(i.operands[n].mem.segment==ZYDIS_REGISTER_GS)segment=2;}
        return "wr::set_reg(s,wr::EAX,m.load(wr::segment_address(s,"+std::string(off)+","+std::to_string(segment)+"),8),8);";
    }
    if(op==ZYDIS_MNEMONIC_MUL || (op==ZYDIS_MNEMONIC_IMUL && i.decoded.operand_count_visible==1))
        return "wr::multiply(s,"+read(i,d)+","+width+","+(op==ZYDIS_MNEMONIC_IMUL?"true":"false")+");";
    if(op==ZYDIS_MNEMONIC_DIV || op==ZYDIS_MNEMONIC_IDIV)
        return "wr::divide(s,"+read(i,d)+","+width+","+(op==ZYDIS_MNEMONIC_IDIV?"true":"false")+");";
    if(op==ZYDIS_MNEMONIC_IMUL) {
        const bool three=i.decoded.operand_count_visible==3;
        return "{auto a="+read(i,three?src:d)+";auto b="+read(i,three?i.operands[2]:src)+";"+arithmetic_flags(d)+"auto v=wr::imul(f,a,b,"+width+");"+write(i,d,"v")+"s.flags=f.flags;s.defined_flags=f.defined_flags;}";
    }
    if(op==ZYDIS_MNEMONIC_SHLD || op==ZYDIS_MNEMONIC_SHRD)
        return "{auto a="+read(i,d)+";auto b="+read(i,src)+";auto c="+read(i,i.operands[2])+";auto f=s;auto v=wr::double_shift(f,a,b,c,"+width+","+(op==ZYDIS_MNEMONIC_SHRD?"true":"false")+");"+write(i,d,"v")+"s.flags=f.flags;s.defined_flags=f.defined_flags;}";
    if(op==ZYDIS_MNEMONIC_SHL || op==ZYDIS_MNEMONIC_SHR || op==ZYDIS_MNEMONIC_SAR || op==ZYDIS_MNEMONIC_ROL || op==ZYDIS_MNEMONIC_ROR || op==ZYDIS_MNEMONIC_RCL || op==ZYDIS_MNEMONIC_RCR) {
        const unsigned kind=op==ZYDIS_MNEMONIC_SHL?0u:op==ZYDIS_MNEMONIC_SHR?1u:op==ZYDIS_MNEMONIC_SAR?2u:op==ZYDIS_MNEMONIC_ROL?3u:op==ZYDIS_MNEMONIC_ROR?4u:op==ZYDIS_MNEMONIC_RCL?5u:6u;
        return "{auto a="+read(i,d)+";auto c="+read(i,src)+";"+arithmetic_flags(d)+"auto v=wr::shift(f,a,c,"+width+","+std::to_string(kind)+");"+write(i,d,"v")+"s.flags=f.flags;s.defined_flags=f.defined_flags;}";
    }
    if(op==ZYDIS_MNEMONIC_BSF || op==ZYDIS_MNEMONIC_BSR)
        return "{auto source="+read(i,src)+";auto old="+read(i,d)+";auto v=wr::scan(s,source,old,"+width+","+(op==ZYDIS_MNEMONIC_BSR?"true":"false")+");"+write(i,d,"v")+"}";
    if(op==ZYDIS_MNEMONIC_BT || op==ZYDIS_MNEMONIC_BTS || op==ZYDIS_MNEMONIC_BTR || op==ZYDIS_MNEMONIC_BTC) {
        const unsigned kind=op==ZYDIS_MNEMONIC_BT?0u:op==ZYDIS_MNEMONIC_BTS?1u:op==ZYDIS_MNEMONIC_BTR?2u:3u;
        std::string result="{auto bit="+read(i,src)+";";
        if(d.type==ZYDIS_OPERAND_TYPE_MEMORY) {
            result+="auto at="+address(i,d)+";";
            if(src.type==ZYDIS_OPERAND_TYPE_REGISTER)result+="at=wr::bit_address(at,bit,"+width+");";
            result+="auto value=m.load(at,"+width+");auto f=s;auto v=wr::bit_op(f,value,bit,"+width+","+std::to_string(kind)+");";
            if(kind)result+="m.store(at,v,"+width+");";else result+="(void)v;";
        } else {
            result+="auto value="+read(i,d)+";auto f=s;auto v=wr::bit_op(f,value,bit,"+width+","+std::to_string(kind)+");";
            if(kind)result+=write(i,d,"v");else result+="(void)v;";
        }
        return result+"s.flags=f.flags;s.defined_flags=f.defined_flags;}";
    }
    if(op==ZYDIS_MNEMONIC_XCHG || op==ZYDIS_MNEMONIC_XADD || op==ZYDIS_MNEMONIC_CMPXCHG) {
        auto destwrite=[&](const std::string& v){return d.type==ZYDIS_OPERAND_TYPE_MEMORY?"m.store(at,"+v+","+width+");":write(i,d,v);};
        std::string result="{";
        if(d.type==ZYDIS_OPERAND_TYPE_MEMORY)result+="auto at="+address(i,d)+";auto a=m.load(at,"+width+");";
        else result+="auto a="+read(i,d)+";";
        result+="auto b="+read(i,src)+";";
        if(op==ZYDIS_MNEMONIC_XCHG)result+=destwrite("b")+write(i,src,"a");
        else if(op==ZYDIS_MNEMONIC_XADD) {
            result+="auto f=s;auto v=wr::add(f,a,b,"+width+");";
            if(d.type==ZYDIS_OPERAND_TYPE_MEMORY)result+=destwrite("v")+write(i,src,"a");
            else result+=write(i,src,"a")+destwrite("v");
            result+="s.flags=f.flags;s.defined_flags=f.defined_flags;";
        }else {
            result+="auto f=s;auto acc=wr::reg(s,wr::EAX,"+width+");(void)wr::sub(f,acc,a,"+width+");if(acc==a){"+destwrite("b")+"}else{";
            // A failed CMPXCHG still performs a destination write cycle.
            if(d.type==ZYDIS_OPERAND_TYPE_MEMORY)result+=destwrite("a");
            result+="wr::set_reg(s,wr::EAX,a,"+width+");}s.flags=f.flags;s.defined_flags=f.defined_flags;";
        }
        return result+"}";
    }

    if(op==ZYDIS_MNEMONIC_NOT)return write(i,d,"~("+read(i,d)+")");
    if(op==ZYDIS_MNEMONIC_CLC)return "s.flags&=~wr::CF;s.defined_flags|=wr::CF;";
    if(op==ZYDIS_MNEMONIC_STC)return "s.flags|=wr::CF;s.defined_flags|=wr::CF;";
    if(op==ZYDIS_MNEMONIC_CMC)return "s.flags^=wr::CF;";
    std::string operation;
    bool store=true;
    switch(op) {
    case ZYDIS_MNEMONIC_ADD:operation="wr::add(f,a,b,"+width+")";break;
    case ZYDIS_MNEMONIC_ADC:operation="wr::add(f,a,b,"+width+",s.flags&wr::CF)";break;
    case ZYDIS_MNEMONIC_SUB:operation="wr::sub(f,a,b,"+width+")";break;
    case ZYDIS_MNEMONIC_SBB:operation="wr::sub(f,a,b,"+width+",s.flags&wr::CF)";break;
    case ZYDIS_MNEMONIC_CMP:operation="wr::sub(f,a,b,"+width+")";store=false;break;
    case ZYDIS_MNEMONIC_AND:operation="wr::logic(f,a&b,"+width+")";break;
    case ZYDIS_MNEMONIC_OR:operation="wr::logic(f,a|b,"+width+")";break;
    case ZYDIS_MNEMONIC_XOR:operation="wr::logic(f,a^b,"+width+")";break;
    case ZYDIS_MNEMONIC_TEST:operation="wr::logic(f,a&b,"+width+")";store=false;break;
    case ZYDIS_MNEMONIC_NEG:operation="wr::sub(f,0,a,"+width+")";break;
    case ZYDIS_MNEMONIC_INC:operation="wr::incdec(f,a,"+width+",false)";break;
    case ZYDIS_MNEMONIC_DEC:operation="wr::incdec(f,a,"+width+",true)";break;
    default:throw std::runtime_error("instruction semantics not implemented");
    }
    auto result="{auto a="+read(i,d)+";";
    if(i.decoded.operand_count_visible>1)result+="auto b="+read(i,src)+";";
    result+=arithmetic_flags(d,store)+"auto v="+operation+";";
    result+=store?write(i,d,"v"):"(void)v;";
    result+="s.flags=f.flags;s.defined_flags=f.defined_flags;}";
    return result;
}
std::string translate(const Instruction& i) {
    const auto statement=translate_unlocked(i);
    const bool implicit=i.decoded.mnemonic==ZYDIS_MNEMONIC_XCHG && (i.operands[0].type==ZYDIS_OPERAND_TYPE_MEMORY || i.operands[1].type==ZYDIS_OPERAND_TYPE_MEMORY);
    if((i.decoded.attributes&ZYDIS_ATTRIB_HAS_LOCK) || implicit)
        return "{auto memory_guard=m.lock();"+statement+"}";
    return statement;
}

}
std::string emit_cpp(const Graph& g,const std::string& name,bool partial) {
    if(name.empty() || !(std::isalpha(static_cast<unsigned char>(name[0])) || name[0]=='_'))throw std::runtime_error("invalid C++ function name");
    for(unsigned char c:name)if(!(std::isalnum(c)||c=='_'))throw std::runtime_error("invalid C++ function name");
    if(g.budget_exhausted || !g.diagnostics.empty())throw std::runtime_error("refusing to lift a graph with discovery errors or an exhausted budget");
    static const std::set<std::string> keywords={"alignas","alignof","and","and_eq","asm","auto","bitand","bitor","bool","break","case","catch","char","char8_t","char16_t","char32_t","class","compl","concept","const","consteval","constexpr","constinit","const_cast","continue","co_await","co_return","co_yield","decltype","default","delete","do","double","dynamic_cast","else","enum","explicit","export","extern","false","float","for","friend","goto","if","inline","int","long","mutable","namespace","new","noexcept","not","not_eq","nullptr","operator","or","or_eq","private","protected","public","register","reinterpret_cast","requires","return","short","signed","sizeof","static","static_assert","static_cast","struct","switch","template","this","thread_local","throw","true","try","typedef","typeid","typename","union","unsigned","using","virtual","void","volatile","wchar_t","while","xor","xor_eq"};
    if(keywords.contains(name) || name[0]=='_' || name.find("__")!=std::string::npos)throw std::runtime_error("reserved C++ function name");
    std::ostringstream out;
    out<<"// Generated by WinRecomp. Guest addresses remain 32-bit.\n#include <winrecomp/sse.hpp>\n";
    std::set<std::string> fp_declarations;for(const auto& [pc,i]:g.instructions){(void)pc;if(i.decoded.opcode_map==ZYDIS_OPCODE_MAP_DEFAULT && i.decoded.opcode>=0xd8 && i.decoded.opcode<=0xdf)fp_declarations.insert(fp_native_name(i));else if(is_sse(i) && i.decoded.mnemonic!=ZYDIS_MNEMONIC_LDMXCSR && i.decoded.mnemonic!=ZYDIS_MNEMONIC_STMXCSR)fp_declarations.insert(sse_native_name(i));}
    for(const auto& declaration:fp_declarations)out<<"extern \"C\" void "<<declaration<<"(void*,void*,std::uint32_t*);\n";
    out<<"void "<<name<<"(wr::Cpu& s,wr::Memory& m,wr::U32 stop,std::uint64_t budget,wr::External external=nullptr,void* user=nullptr) {\nwhile(s.eip!=stop) {\nwr::tick(budget,s.eip);\nswitch(s.eip) {\n";
    for(const auto& [pc,i]:g.instructions) {
        std::string statement;
        try{statement=translate(i);}catch(const std::exception& e){
            const auto message="cannot lift "+hex(pc)+" ("+i.text+"): "+e.what();
            if(!partial)throw std::runtime_error(message);
            statement="throw wr::GuestFault(wr::FaultKind::unsupported,s.eip,"+quote(message)+");";
        }
        out<<"case "<<number(pc)<<": { "<<statement<<"\n";
        if(i.flow==Flow::normal)out<<"s.eip="<<number(i.next())<<";\n";
        out<<"break; }\n";
    }
    out<<"default:if(external && external(s,m,user))break;throw wr::GuestFault(wr::FaultKind::untranslated,s.eip,\"untranslated guest target\");\n}\n}\n}\n";return out.str();
}
std::string coverage_json(const Graph& g) {
    std::map<std::string,std::size_t> failures;std::size_t supported=0;
    for(const auto& [a,i]:g.instructions) {
        (void)a;try{(void)translate(i);++supported;}catch(const std::exception& e){++failures[std::string(ZydisMnemonicGetString(i.decoded.mnemonic))+": "+e.what()];}
    }
    std::ostringstream out;out<<"{\"schema\":\"winrecomp.coverage.v1\",\"admitted_instructions\":"<<g.instructions.size()<<",\"supported_instruction_forms\":"<<supported<<",\"not_a_game_completion_percentage\":true,\"unsupported\":[";
    bool first=true;for(const auto& [why,n]:failures){if(!first)out<<',';first=false;out<<"{\"reason\":"<<quote(why)<<",\"count\":"<<n<<'}';}
    out<<"]}\n";return out.str();
}
void emit_project(const Image& image,const Graph& graph,const std::filesystem::path& output,bool partial) {
    if(graph.entry!=image.entry)throw std::runtime_error("program project requires the original PE entry point");
    if(graph.budget_exhausted || !graph.diagnostics.empty())throw std::runtime_error("project generation requires a complete, diagnostic-free graph");
    if(std::filesystem::exists(output))throw std::runtime_error("project output directory already exists; generation never overwrites an existing tree");
    std::map<Address,std::ostringstream> chunks;std::map<Address,std::set<std::string>> declarations;
    for(const auto& [pc,i]:graph.instructions) {
        std::string statement;
        try{statement=translate(i);}catch(const std::exception& e) {
            const auto why="cannot translate "+hex(pc)+" ("+i.text+"): "+e.what();
            if(!partial)throw std::runtime_error(why);
            statement="throw wr::GuestFault(wr::FaultKind::unsupported,s.eip,"+quote(why)+");";
        }
        const auto page=pc>>12;
        if(i.decoded.opcode_map==ZYDIS_OPCODE_MAP_DEFAULT && i.decoded.opcode>=0xd8 && i.decoded.opcode<=0xdf)declarations[page].insert(fp_native_name(i));
        else if(is_sse(i) && i.decoded.mnemonic!=ZYDIS_MNEMONIC_LDMXCSR && i.decoded.mnemonic!=ZYDIS_MNEMONIC_STMXCSR)declarations[page].insert(sse_native_name(i));
        auto& out=chunks[page];out<<"case "<<number(pc)<<": { wr::check_code(m,s.eip,{";
        bool first=true;for(auto byte:i.bytes){if(!first)out<<',';first=false;out<<unsigned(byte);}
        out<<"});\n"<<statement<<"\n";
        if(i.flow==Flow::normal)out<<"s.eip="<<number(i.next())<<";\n";
        out<<"return true; }\n";
    }
    std::map<std::string,std::string> files;std::ostringstream dispatch,cmake;
    dispatch<<"#include <winrecomp/compiled.hpp>\n";
    cmake<<"cmake_minimum_required(VERSION 3.20)\nproject(RecompiledProgram LANGUAGES C CXX)\nset(CMAKE_CXX_STANDARD 20)\nset(CMAKE_CXX_STANDARD_REQUIRED ON)\nset(WINRECOMP_SOURCE \"\" CACHE PATH \"Path to the matching WinRecomp checkout\")\nif(NOT EXISTS \"${WINRECOMP_SOURCE}/CMakeLists.txt\")\nmessage(FATAL_ERROR \"Set WINRECOMP_SOURCE to the WinRecomp source tree\")\nendif()\nset(BUILD_TESTING OFF CACHE BOOL \"\" FORCE)\nadd_subdirectory(\"${WINRECOMP_SOURCE}\" winrecomp)\nadd_executable(recompiled_program main.cpp dispatch.cpp\n";
    for(const auto& [page,body]:chunks) {
        const auto name="code_page_"+std::to_string(page);std::ostringstream source;
        source<<"#include <winrecomp/compiled.hpp>\n#include <winrecomp/sse.hpp>\n";
        for(const auto& symbol:declarations[page])source<<"extern \"C\" void "<<symbol<<"(void*,void*,std::uint32_t*);\n";
        source<<"bool "<<name<<"(wr::Cpu& s,wr::Memory& m,std::uint64_t& budget) { (void)budget;switch(s.eip) {\n"<<body.str()<<"default:return false;\n}}\n";
        files[name+".cpp"]=source.str();dispatch<<"bool "<<name<<"(wr::Cpu&,wr::Memory&,std::uint64_t&);\n";cmake<<name<<".cpp\n";
    }
    dispatch<<"bool compiled_step(wr::Cpu& s,wr::Memory& m,std::uint64_t& budget) { switch(s.eip>>12) {\n";
    for(const auto& [page,body]:chunks){(void)body;dispatch<<"case "<<page<<":return code_page_"<<page<<"(s,m,budget);\n";}
    dispatch<<"default:return false;\n}}\n";files["dispatch.cpp"]=dispatch.str();
    cmake<<")\ntarget_link_libraries(recompiled_program PRIVATE winrecomp_guest)\n";files["CMakeLists.txt"]=cmake.str();
    files["main.cpp"]="#include <winrecomp/process.hpp>\nbool compiled_step(wr::Cpu&,wr::Memory&,std::uint64_t&);\nint main(int argc,char** argv){return wr::run_program(argc,argv,compiled_step,"+quote(sha256(image.bytes))+");}\n";
    files["manifest.json"]="{\"schema\":\"winrecomp.program.v1\",\"sha256\":"+quote(sha256(image.bytes))+",\"fixed_image_base\":"+std::to_string(image.base)+",\"entry\":"+std::to_string(graph.entry)+",\"instructions\":"+std::to_string(graph.instructions.size())+",\"partial\":"+(partial?"true":"false")+",\"instruction_guard\":\"compare original bytes before each translated instruction\"}\n";
    files["README.md"]="# Generated native program\n\nThis is private, input-derived output. Do not add it or the input executable to the WinRecomp repository. Build with CMake and -DWINRECOMP_SOURCE=<matching source checkout>. Run recompiled_program <original.exe> --root <data-directory> --report <report.json>. The runtime checks the SHA-256 of the original file, then executes generated native code, not its x86 instructions. Unknown imports, unknown code targets, changed code bytes, and unsupported exceptions stop explicitly. Filesystem writes are disabled unless --allow-write is supplied. This is not an OS security sandbox; run only inputs you trust.\n";
    // Preflight every translation before creating any output. A failed write
    // removes only the directory this call created, never a preexisting tree.
    if(!std::filesystem::create_directories(output))throw std::runtime_error("cannot create new project directory");
    try{for(const auto& [path,content]:files)write_file(output/path,content);}catch(...){std::filesystem::remove_all(output);throw;}
}

}
