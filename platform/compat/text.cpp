#include "text.hpp"
#include "codepage.hpp"
#include "../../port/guest_api.hpp"
#include <algorithm>
namespace wa2::platform::compat {
namespace {
using namespace wr;using namespace wr::port;
constexpr U32 staging=1048576;
U32 cp_number(U32 cp){return cp==0 || cp==1 || cp==3?936:cp;}
U32 failure(Process& p,U32 code){p.set_error(code);return 0;}
std::vector<std::uint8_t> narrow(Process& p,U32 at,U32 size){
    if(!at || !size || (size>staging && size!=0xffffffffu))throw EncodingError("invalid source byte count");
    if(size==0xffffffffu)size=U32(p.read_string(at,staging-1).size()+1);std::vector<std::uint8_t> value(size);p.memory.copy_out(at,value);return value;
}
std::u16string wide(Process& p,U32 at,U32 size){
    if(!at || !size || (size>staging/2 && size!=0xffffffffu))throw EncodingError("invalid source wide count");
    if(size==0xffffffffu){auto value=p.read_wstring(at,staging/2-1);value+=char16_t(0);return value;}
    std::u16string value(size,u'\0');for(U32 n=0;n<size;++n)value[n]=char16_t(p.memory.load(at+n*2,16));return value;
}
void bind_api(Process& p,const char* name,unsigned count,std::function<U32(Process&,Args)> function){
    auto wrapper=[count,function=std::move(function)](Process& q){std::array<U32,8> args{};for(unsigned n=0;n<count;++n)args[n]=q.argument(n);return function(q,std::span(args).first(count));};
    p.replace_api("kernel32.dll",name,count,std::move(wrapper));
}
U32 get_cp(Process& q,Args a){const auto cp=cp_number(a[0]);if(cp!=65001 && !codepage(cp))return failure(q,87);
    std::array<std::uint8_t,20> info{};info[0]=cp==65001?4:cp==932 || cp==936?2:1;info[4]='?';
    if(cp==936){info[6]=0x81;info[7]=0xfe;}else if(cp==932){info[6]=0x81;info[7]=0x9f;info[8]=0xe0;info[9]=0xfc;}
    q.memory.copy_in(a[1],info);return 1;
}
std::uint16_t classify(char16_t scalar,U32 type){
    return character_type(scalar,type);
}
char16_t change_case(char16_t scalar,bool upper){return character_case(scalar,upper);}
}
void install_text(wr::Process& p){
    bind_api(p,"GetACP",0,[](Process&,Args){return 936u;});bind_api(p,"GetOEMCP",0,[](Process&,Args){return 936u;});
    bind_api(p,"GetCPInfo",2,get_cp);
    bind_api(p,"IsValidCodePage",1,[](Process&,Args a){return a[0]==65001 || codepage(a[0])?1u:0u;});
    bind_api(p,"IsValidLocale",2,[](Process&,Args a){return (a[1]&~3u)?0u:a[0]==0x409 || a[0]==0x804 || a[0]==0x411 || a[0]==0x7f?1u:0u;});
    bind_api(p,"MultiByteToWideChar",6,[](Process& q,Args a){const auto cp=cp_number(a[0]);if((cp!=65001 && !codepage(cp)) || a[3]==0 || (a[3]>staging && a[3]!=0xffffffffu) || a[5]>staging/2 || !a[2])return failure(q,87);
        if((cp==65001 && (a[1]&~8u)) || (cp!=65001 && (a[1]&~9u)))return failure(q,1004);
        try{const auto text=decode_text(cp,narrow(q,a[2],a[3]),bool(a[1]&8));if(!a[5])return U32(text.size());if(text.size()>a[5])return failure(q,122);
            q.memory.check(a[4],text.size()*2,Memory::Write);for(unsigned n=0;n<text.size();++n)q.memory.store(a[4]+n*2,text[n],16);return U32(text.size());}
        catch(const EncodingError&){return failure(q,1113);}
    });
    bind_api(p,"WideCharToMultiByte",8,[](Process& q,Args a){const auto cp=cp_number(a[0]);if((cp!=65001 && !codepage(cp)) || !a[2] || a[3]==0 || (a[3]>staging/2 && a[3]!=0xffffffffu) || a[5]>staging || (cp==65001 && (a[6] || a[7])))return failure(q,87);
        if((cp==65001 && (a[1]&~0x80u)) || (cp!=65001 && (a[1]&~0x400u)))return failure(q,1004);
        std::array<std::uint8_t,2> fallback{};unsigned size{};if(a[6]){fallback[0]=std::uint8_t(q.memory.load(a[6],8));size=1;
            if((cp==936 && fallback[0]>=0x81 && fallback[0]<=0xfe) || (cp==932 && ((fallback[0]>=0x81 && fallback[0]<=0x9f) || (fallback[0]>=0xe0 && fallback[0]<=0xfc)))){fallback[1]=std::uint8_t(q.memory.load(a[6]+1,8));size=2;}}
        if(a[7])q.memory.check(a[7],4,Memory::Write);
        try{auto output=encode_text(cp,wide(q,a[2],a[3]),std::span(fallback).first(size),bool(a[1]&0x80));if(a[5] && a[5]<output.bytes.size())return failure(q,122);
            if(a[5])q.memory.copy_in(a[4],output.bytes);if(a[7])q.memory.store(a[7],output.substituted,32);return U32(output.bytes.size());}
        catch(const EncodingError&){return failure(q,1113);}
    });
    for(bool unicode:{false,true})bind_api(p,unicode?"GetStringTypeW":"GetStringTypeA",unicode?4:5,[unicode](Process& q,Args a){
        const auto type=a[unicode?0:1],source=a[unicode?1:2],count=a[unicode?2:3],destination=a[unicode?3:4];if(type!=1 && type!=2 && type!=4)return failure(q,1004);
        if(source==destination || !count)return failure(q,87);try{const auto text=unicode?wide(q,source,count):decode_text(936,narrow(q,source,count));std::vector<std::uint16_t> kinds;for(auto c:text)kinds.push_back(classify(c,type));
            q.memory.check(destination,kinds.size()*2,Memory::Write);for(unsigned n=0;n<kinds.size();++n)q.memory.store(destination+n*2,kinds[n],16);return 1u;}
        catch(const EncodingError& e){throw GuestFault(FaultKind::unsupported,q.cpu.eip,e.what());}
    });
    for(bool unicode:{false,true})bind_api(p,unicode?"LCMapStringW":"LCMapStringA",6,[unicode](Process& q,Args a){
        if(a[1]!=0x100 && a[1]!=0x200)throw GuestFault(FaultKind::unsupported,q.cpu.eip,"common text adapter requires an explicit collation/transformation contract");
        if(!a[3] || a[5]>staging)return failure(q,87);try{auto text=unicode?wide(q,a[2],a[3]):decode_text(936,narrow(q,a[2],a[3]));for(auto& c:text)c=change_case(c,a[1]==0x200);
            if(unicode){if(a[5] && text.size()>a[5])return failure(q,122);if(a[5]){q.memory.check(a[4],text.size()*2,Memory::Write);for(unsigned n=0;n<text.size();++n)q.memory.store(a[4]+n*2,text[n],16);}return U32(text.size());}
            const auto output=encode_text(936,text);if(a[5] && output.bytes.size()>a[5])return failure(q,122);if(a[5])q.memory.copy_in(a[4],output.bytes);return U32(output.bytes.size());}
        catch(const EncodingError& e){throw GuestFault(FaultKind::unsupported,q.cpu.eip,e.what());}
    });
    p.state().reports["text_adapter"]=[]{return std::string("{\"backend\":\"common-codec-tables\",\"acp\":936,\"codepages\":[437,1252,932,936,65001],\"windows_nls_calls\":false}");};
}
}
