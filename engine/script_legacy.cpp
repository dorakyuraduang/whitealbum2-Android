#include "script_legacy.hpp"
#include "checkpoint_codec.hpp"
#include <algorithm>
#include <bit>
#include <map>

namespace wa2::script {
namespace {
constexpr std::size_t record_bytes=0xd190;
class Record {
    std::span<const std::uint8_t> data_;
public:
    explicit Record(std::span<const std::uint8_t> data):data_(data){if(data.size()!=record_bytes)throw Error("Truncated original script record");}
    std::uint32_t word(std::size_t at)const{checkpoint::Reader in(data_.subspan(at,4));return in.u32();}
    std::int32_t integer(std::size_t at)const{return std::bit_cast<std::int32_t>(word(at));}
    std::string text(std::size_t at,std::size_t limit)const{
        const auto field=data_.subspan(at,limit);const auto end=std::find(field.begin(),field.end(),std::uint8_t(0));
        if(end==field.end())throw Error("Unterminated original script name");
        return {reinterpret_cast<const char*>(field.data()),std::size_t(end-field.begin())};
    }
};
std::string terminated(checkpoint::Reader& in,std::size_t maximum){
    const auto size=in.u32();if(!size || size>maximum)throw Error("Original global text exceeds its size limit");
    const auto bytes=in.raw(size);
    if(bytes.back() || std::find(bytes.begin(),bytes.end()-1,std::uint8_t(0))!=bytes.end()-1)throw Error("Invalid original global text termination");
    return {reinterpret_cast<const char*>(bytes.data()),bytes.size()-1};
}
struct GlobalImport {Globals globals;std::vector<std::uint32_t> indices;};
GlobalImport globals(std::span<const std::uint8_t> bytes,const Globals& schema){
    checkpoint::Reader in(bytes);if(in.u32()!=bytes.size())throw Error("Original global block length mismatch");
    const auto count=in.u32();if(count>1024 || count!=schema.variables().size())throw Error("Original global catalogue count changed");
    std::map<std::string,std::uint32_t,std::less<>> names;
    for(unsigned n=0;n<schema.variables().size();++n)if(!names.emplace(schema.variables()[n].name,n).second)throw Error("Duplicate native global catalogue name");
    GlobalImport next{schema,{}};next.indices.reserve(count);
    for(unsigned n=0;n<count;++n){
        const auto name=terminated(in,4097);const auto found=names.find(name);
        if(found==names.end())throw Error("Unknown or duplicate original global name");
        const auto index=found->second;names.erase(found);const auto type=in.raw(1)[0];
        if(type!=std::uint32_t(schema.variables()[index].storage))throw Error("Original global storage type changed");
        Value value;
        switch(type){
        case 1:value=Value(std::int32_t(in.raw(1)[0]));break;
        case 2:{const auto b=in.raw(2);value=Value(std::int32_t(std::bit_cast<std::int16_t>(std::uint16_t(unsigned(b[0])|(unsigned(b[1])<<8)))));break;}
        case 3:value=Value(in.i32());break;
        case 4:value=Value(std::bit_cast<float>(in.u32()));break;
        case 5:value=Value(terminated(in,1024u*1024+1));break;
        default:throw Error("Unknown original global storage type");
        }
        next.globals.set(index,value);next.indices.push_back(index);
    }
    in.finish();return next;
}
void value(checkpoint::Writer& out,const Value& input){
    out.u32(std::uint32_t(input.type()));out.boolean(input.uses_default_text());
    if(input.type()==Type::text)out.string(input.text());else if(input.type()==Type::real)out.u32(std::bit_cast<std::uint32_t>(input.real()));else out.i32(input.integer());
}
Value owned_value(const Record& record,std::size_t at){
    switch(record.word(at+4)){
    case 1:case 2:case 3:return Value(record.integer(at+16));
    case 4:return Value(std::bit_cast<float>(record.word(at+12)));
    case 5:
        if(record.word(at+8))throw Unsupported("Original saved stack text has no serialized contents");
        if(record.integer(at+16)==-1)return Value::default_text();
        if(record.word(at+16))throw Error("Invalid original saved text value");
        return Value(std::string{});
    default:throw Error("Unknown original saved stack value type");
    }
}
std::vector<std::uint8_t> runtime(const Record& record,const std::shared_ptr<const Program>& source,const Functions& functions,
        const GlobalImport& imported,const LegacyResumeText& text,bool has_child){
    const auto& program=*source;
    if(record.word(4)!=program.version() || record.word(8)!=program.labels().size())throw Error("Original saved script version or label catalogue changed");
    for(unsigned n=0;n<program.labels().size();++n)if(record.integer(12+n*4)!=program.labels()[n].id || record.word(0x40c+n*4)!=program.labels()[n].offset)throw Error("Original saved script label table changed");
    program.label(record.integer(0x8b40));
    const auto state=record.word(0x80c);if(state!=1 && state!=2)throw Error("Invalid original saved script running state");
    if(record.word(0xd048))throw Unsupported("Original save contains an unfinished scene replacement");
    const bool pending=record.word(0x8b34)!=0;
    if(pending && (state!=2 || record.word(0x8b38)!=4 || !record.word(0x8b44)))throw Error("Invalid original suspended command state");
    if(has_child && (pending || state!=2))throw Error("Original saved caller is not suspended behind its child");
    checkpoint::Writer out;out.string(program.name());out.u32(program.version());out.u64(program.fingerprint());
    const auto offset=record.word(0x8cac);
    if(!program.instruction_offset(offset,state==1))throw Error("Original saved script position is not an instruction boundary");
    out.u32(offset);out.i32(record.integer(0x8ca8));out.boolean(state==2);out.u64(0);
    const auto depth=record.word(0xa0b0);if(depth>255 || record.word(0x8cb0))throw Error("Invalid original script value stack depth or sentinel");out.u32(depth);
    for(unsigned n=0;n<=depth;++n){
        const auto at=0x8cb0+n*20;const auto kind=record.word(at);auto reference=record.word(at+16);std::int32_t local=-1;Value data;
        switch(kind){
        case 0:if(n)throw Error("Empty original active script stack slot");reference=0;break;
        case 1:if(reference>=imported.indices.size())throw Error("Original global stack reference exceeds catalogue");reference=imported.indices[reference];break;
        case 2:local=record.integer(at+4);if(local<-1 || local>15 || reference>=52)throw Error("Original local stack reference exceeds bounds");break;
        case 3:if(reference>=program.strings().size())throw Error("Original source text reference exceeds catalogue");break;
        case 5:data=owned_value(record,at);reference=0;break;
        default:throw Error("Unknown original script stack kind");
        }
        out.u32(kind);out.i32(local);out.u32(reference);value(out,data);
    }
    for(unsigned n=0;n<26*16;++n)out.u32(record.word(0xa0b4+n*4));
    for(unsigned n=0;n<26*16;++n)out.u32(record.word(0xa734+n*4));
    const auto flows=record.word(0xcef4);if(flows>15 || record.word(0xadb4))throw Error("Invalid original script flow depth or sentinel");out.u32(flows);
    for(unsigned n=0;n<=flows;++n){
        const auto at=0xadb4+n*0x214;const auto kind=record.word(at);
        if(n && kind!=2 && kind!=3 && kind!=5 && kind!=6 && kind!=7)throw Error("Unknown original script flow kind");
        const auto count=kind==7?record.word(at+8):0;if(count>64)throw Error("Original saved switch exceeds case limit");
        // Original records retain stale unused members. Translate the fields
        // consumed by this flow kind, not the complete old object layout.
        out.u32(kind);out.u32(n?record.word(at+12):0);out.u32(kind==2 || kind==3 || kind==5 || kind==6?record.word(at+16):0);
        out.u32(kind==5?record.word(at+20):0);out.u32(kind==5?record.word(at+24):0);out.i32(n?record.integer(at+0x210):0);out.u32(count);
        for(unsigned c=0;c<count;++c){out.u32(record.word(at+16+c*4));out.i32(record.integer(at+0x110+c*4));}
    }
    out.boolean(pending);
    if(pending){
        const auto index=record.word(0x8b3c);const auto& function=functions.at(index);
        if(index<=2)throw Error("Original intrinsic call unexpectedly retained a continuation");
        bool site=false;for(const auto& command:program.commands())if(command.offset+8==offset && command.index==index){site=true;break;}
        if(!site)throw Error("Original suspended command does not match its script position");
        out.u32(index);out.u32(record.word(0x8b44));out.u32(std::uint32_t(function.parameters.size()));
        for(unsigned n=0;n<function.parameters.size();++n){
            const auto at=0x8b4c+n*20;const auto type=function.parameters[n];const auto saved_type=record.word(at+4);Value data;
            if(type==5){
                if(saved_type!=5)throw Error("Original suspended text argument type changed");
                auto restored=text?text(program,index,n):std::nullopt;
                if(!restored)throw Unsupported("Original suspended command text needs a scene-state resolver");
                if(restored->size()>1024u*1024 || restored->find('\0')!=std::string::npos)throw Error("Invalid resolved original command text");data=Value(std::move(*restored));
            }else if(type==4){if(saved_type!=4)throw Error("Original suspended real argument type changed");data=Value(std::bit_cast<float>(record.word(at+12)));}
            else if(type<=3 || type==7){if(saved_type!=3)throw Error("Original suspended integer argument type changed");data=Value(record.integer(at+16));}
            else throw Unsupported("Unsupported original suspended command parameter type");
            value(out,data);
        }
    }
    auto result=out.seal("LSRT");auto validation_globals=imported.globals;Runtime validation(source,functions,validation_globals);validation.restore(result);return validation.checkpoint();
}
}
LegacyScriptImport import_legacy_script(std::span<const std::uint8_t> bytes,const Functions& functions,
        const Globals& schema,const Loader& load,LegacyResumeText text){
    if(!load || bytes.size()>checkpoint::maximum_bytes || bytes.size()<12)throw Error("Missing loader or invalid original script save length");
    checkpoint::Reader header(bytes);const auto version=header.raw(4);
    if(!std::equal(version.begin(),version.end(),"1.00"))throw Error("Unknown original script save version");
    const auto slots=header.u32(),length=header.u32();
    if(slots>4 || length!=12+(slots+1)*record_bytes || length>bytes.size() || bytes.size()-length<8)throw Error("Invalid original script save record count or length");
    checkpoint::Reader tail(bytes.subspan(length));const auto globals_length=tail.u32();
    if(globals_length<8 || globals_length>bytes.size()-length)throw Error("Invalid original global save block length");
    auto imported=globals(bytes.subspan(length,globals_length),schema);checkpoint::Writer out;out.blob(imported.globals.checkpoint());
    std::vector<std::span<const std::uint8_t>> frames;bool follows=true;
    for(unsigned n=0;n<=slots;++n){const auto data=bytes.subspan(12+n*record_bytes,record_bytes);
        if(!follows){if(std::any_of(data.begin(),data.end(),[](auto c){return c!=0;}))throw Error("Nonempty original script record after call-chain end");continue;}
        frames.push_back(data);follows=Record(data).word(0xd044)!=0;
    }
    if(follows)throw Error("Original saved call chain exceeds its record count");
    LegacyScriptImport result;result.consumed=length+globals_length;out.u32(std::uint32_t(frames.size()));
    for(unsigned n=0;n<frames.size();++n){
        const Record record(frames[n]);auto name=record.text(0xcef8,64);auto group=record.text(0xcf3c,256);
        if(name.empty())throw Error("Empty original saved script name");
        for(unsigned char c:name)if(!((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_' || c=='-'))throw Error("Invalid original saved script name");
        for(auto& c:group)if(c>='A' && c<='Z')c+=char('a'-'A');if(group!="ck-gal")throw Unsupported("Original saved script resource group is not supported");
        auto program=load(name);if(!program || program->name()!=name)throw Error("Original save loader returned a different script");
        out.string(name);out.blob(runtime(record,program,functions,imported,text,n+1<frames.size()));result.scenes.push_back(std::move(name));
        if(n+1==frames.size() && record.word(0x8b34))result.pending_command=record.word(0x8b3c);
    }
    result.checkpoint=out.seal("LSSN");return result;
}
}
