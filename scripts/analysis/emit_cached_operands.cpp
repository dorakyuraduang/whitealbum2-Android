// Offline emitter for already-decoded cached operands. No decoder or image load.
// Reuse the existing lifter; this helper is never linked into either game host.
#include "tools/WindowsRecompTool-main/src/lift.cpp"
#include <fstream>
#include <iostream>

namespace wr {
void write_file(const std::filesystem::path&,const std::string&){throw std::logic_error("project emission is disabled in the cached-operand tool");}
std::string hex(std::uint64_t value){std::ostringstream out;out<<"0x"<<std::hex<<value;return out.str();}
std::string quote(const std::string& value){
    std::string out="\"";for(unsigned char c:value){if(c=='\\' || c=='\"')out+='\\';if(c=='\n')out+="\\n";else out+=c;}return out+'\"';
}
}
template<class Enum,class Getter> Enum named(const std::string& name,unsigned end,Getter getter){
    for(unsigned n=0;n<=end;++n){const char* value=getter(Enum(n));if(value && name==value)return Enum(n);}
    throw std::runtime_error("unknown cached enum: "+name);
}
ZydisRegister reg(const std::string& name){return named<ZydisRegister>(name,ZYDIS_REGISTER_MAX_VALUE,ZydisRegisterGetString);}
int main(int argc,char** argv){try{
    if(argc!=3)throw std::runtime_error("decoded input and C++ output paths required");
    std::ifstream input(argv[1]);std::ofstream output(argv[2]);if(!input || !output)throw std::runtime_error("cannot open emitter inputs");
    std::string line;
    while(std::getline(input,line)){
        std::istringstream row(line);wr::Instruction i;std::string bytes,mnemonic;unsigned width,opcode,modrm,repeat,count;
        if(!(row>>i.address>>bytes>>mnemonic>>width>>opcode>>modrm>>repeat>>count))throw std::runtime_error("bad cached instruction row");
        if(count>3 || bytes.empty() || bytes.size()%2 || bytes.size()>30)throw std::runtime_error("invalid cached instruction shape");
        for(std::size_t n=0;n<bytes.size();n+=2)i.bytes.push_back(std::uint8_t(std::stoul(bytes.substr(n,2),nullptr,16)));
        i.decoded.length=std::uint8_t(i.bytes.size());i.decoded.address_width=32;i.decoded.operand_width=width;
        i.decoded.mnemonic=named<ZydisMnemonic>(mnemonic,ZYDIS_MNEMONIC_MAX_VALUE,ZydisMnemonicGetString);
        i.decoded.opcode=opcode;i.decoded.raw.modrm.offset=modrm;i.decoded.operand_count_visible=count;i.decoded.operand_count=count;
        i.flow=wr::Flow::normal;i.text=mnemonic;
        if(repeat){
            if(mnemonic!="scasb" || bytes!="f2ae" || repeat!=2 || count)throw std::runtime_error("unsupported cached string form");
            i.decoded.meta.category=ZYDIS_CATEGORY_STRINGOP;i.decoded.attributes=ZYDIS_ATTRIB_HAS_REPNE;
        }
        for(unsigned n=0;n<count;++n){
            char kind;unsigned size,scale;std::string name,base,index,segment;std::int64_t displacement;std::uint64_t immediate;
            if(!(row>>kind>>size>>name>>base>>index>>scale>>displacement>>immediate>>segment))throw std::runtime_error("bad cached operand");
            auto& op=i.operands[n];op.size=size;op.actions=ZYDIS_OPERAND_ACTION_READ;
            if(kind=='r'){op.type=ZYDIS_OPERAND_TYPE_REGISTER;op.reg.value=reg(name);}
            else if(kind=='i'){op.type=ZYDIS_OPERAND_TYPE_IMMEDIATE;op.imm.value.u=immediate;}
            else if(kind=='m'){
                op.type=ZYDIS_OPERAND_TYPE_MEMORY;op.mem.base=reg(base);op.mem.index=reg(index);op.mem.scale=scale;
                op.mem.segment=reg(segment);op.mem.disp.has_displacement=true;op.mem.disp.value=displacement;
            }else throw std::runtime_error("unknown cached operand kind");
        }
        std::string extra;if(row>>extra)throw std::runtime_error("trailing cached operand data");
        output<<"case "<<wr::hex(i.address)<<"u: { "<<wr::translate(i)<<"\ns.eip="<<wr::hex(i.next())<<"u;\nreturn true; }\n";
    }
    if(!output.good())throw std::runtime_error("cached emitter output failed");
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
