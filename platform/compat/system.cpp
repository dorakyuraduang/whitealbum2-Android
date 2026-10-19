#include "system.hpp"
#include "profile.hpp"
#include "codepage.hpp"
#include "resource_mount.hpp"
#include "../../port/guest_api.hpp"
#include "../clock.hpp"
#include <winrecomp/resources.hpp>
#include <algorithm>
#include <bit>
#include <chrono>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <sstream>
#include <thread>
namespace wa2::platform::compat {
namespace {
using namespace wr;using namespace wr::port;
void bind(Process& p,const char* dll,const std::string& name,unsigned count,std::function<U32(Process&,Args)> fn){
    const auto existing=p.resolve(dll,name);
    auto invoke=[count,fn=std::move(fn)](Process& q){std::array<U32,32> args{};for(unsigned n=0;n<count;++n)args[n]=q.argument(n);return fn(q,std::span(args).first(count));};
    if(existing)p.replace_api(dll,name,count,std::move(invoke));else p.register_api(dll,name,count,std::move(invoke));
}
U32 error(Process& p,U32 value){p.set_error(value);return 0;}
U32 output_text(Process& p,U32 at,U32 count,std::string_view text){
    if(!count)return 0;const auto length=U32(std::min<std::size_t>(count-1,text.size()));p.memory.check(at,length+1,Memory::Write);
    p.memory.copy_in(at,{reinterpret_cast<const std::uint8_t*>(text.data()),length});p.memory.store(at+length,0,8);return length;
}
U32 output_names(Process& p,U32 at,U32 count,const std::vector<std::string>& names){
    if(!count)return 0;std::string bytes;for(const auto& name:names){bytes+=name;bytes+='\0';}bytes+='\0';if(names.empty())bytes+='\0';
    if(bytes.size()<=count){p.memory.copy_in(at,{reinterpret_cast<const std::uint8_t*>(bytes.data()),bytes.size()});return names.empty()?0:U32(bytes.size()-1);}
    p.memory.check(at,count,Memory::Write);if(count<2){p.memory.store(at,0,8);return 0;}
    p.memory.copy_in(at,{reinterpret_cast<const std::uint8_t*>(bytes.data()),count-2});p.memory.store(at+count-2,0,16);return count-2;
}
ResourceName resource_name(Process& p,U32 value){
    if(value<65536)return std::uint16_t(value);const auto bytes=p.read_string(value);
    return decode_text(936,{reinterpret_cast<const std::uint8_t*>(bytes.data()),bytes.size()},true);
}
struct Resources {struct Item{Resource resource;U32 loaded{};};std::map<U32,Item> items;U32 next=0xba000000;};
struct Fibers {struct Slot {U32 callback{};std::map<U32,U32> values;};std::map<U32,Slot> slots;U32 cookie{};};
std::string format_text(Process& p,U32 format,U32 arguments){
    const auto text=p.read_string(format,65536);std::string output;std::size_t index{};
    std::uint64_t cursor=arguments;auto next=[&]{if(cursor+4>0x100000000ull)throw GuestFault(FaultKind::memory,U32(cursor),"format arguments wrap source address space");const auto value=p.memory.load(U32(cursor),32);cursor+=4;return value;};
    auto append=[&](std::string_view value){if(value.size()>1023-output.size())throw GuestFault(FaultKind::unsupported,p.cpu.eip,"wvsprintf output exceeds 1023 bytes");output+=value;};
    if(text=="%s"){
        const auto at=next();output=at?p.read_string(at,65536):"(null)";
        if(output.size()>1023)throw GuestFault(FaultKind::unsupported,p.cpu.eip,"wvsprintf output exceeds 1023 bytes");
        return output;
    }
    while(index<text.size()){
        if(text[index]!='%'){append(text.substr(index++,1));continue;}++index;if(index==text.size())break;
        if(text[index]=='%'){append("%");++index;continue;}
        bool left=false,zero=false,alternate=false,plus=false,blank=false;
        while(index<text.size()){
            const char c=text[index];if(c=='-')left=true;else if(c=='0')zero=true;else if(c=='#')alternate=true;else if(c=='+')plus=true;else if(c==' ')blank=true;else break;++index;
        }
        int width{};if(index<text.size() && text[index]=='*'){width=std::bit_cast<std::int32_t>(next());++index;if(width<0){left=true;if(width==std::numeric_limits<int>::min())throw std::runtime_error("format width overflow");width=-width;}}
        else while(index<text.size() && text[index]>='0' && text[index]<='9'){if(width>1023)throw std::runtime_error("format width limit");width=width*10+text[index++]-'0';}
        int precision=-1;if(index<text.size() && text[index]=='.'){++index;precision=0;if(index<text.size() && text[index]=='*'){precision=std::bit_cast<std::int32_t>(next());++index;}
            else while(index<text.size() && text[index]>='0' && text[index]<='9'){if(precision>1023)throw std::runtime_error("format precision limit");precision=precision*10+text[index++]-'0';}}
        if(width>1023 || precision>1023)throw std::runtime_error("format staging limit");
        bool wide=false,short_value=false,int64=false;if(index<text.size() && text[index]=='l'){wide=true;++index;}else if(index<text.size() && text[index]=='h'){short_value=true;++index;}
        else if(text.substr(index,3)=="I64"){int64=true;index+=3;}else if(text.substr(index,3)=="I32")index+=3;
        if(index==text.size())break;const auto spec=text[index++];std::string value,prefix;
        if(spec=='s' || spec=='S'){
            const auto at=next();const bool unicode=wide || (spec=='S' && !short_value);
            if(!at)value="(null)";else if(unicode){const auto source=p.read_wstring(at,65536);const auto encoded=encode_text(936,source);value.assign(encoded.bytes.begin(),encoded.bytes.end());}
            else value=p.read_string(at,65536);if(precision>=0 && value.size()>unsigned(precision))value.resize(unsigned(precision));
        }else if(spec=='c' || spec=='C'){const auto c=next();if(wide || (spec=='C' && !short_value)){const auto encoded=encode_text(936,std::u16string(1,char16_t(c)));value.assign(encoded.bytes.begin(),encoded.bytes.end());}else value+=char(c);}
        else if(spec=='d' || spec=='i' || spec=='u' || spec=='x' || spec=='X' || spec=='o' || spec=='p'){
            std::uint64_t number=next();if(int64)number|=std::uint64_t(next())<<32;
            const bool signed_value=spec=='d' || spec=='i';bool negative=false;
            if(signed_value){if(short_value){auto n=std::int16_t(number);negative=n<0;number=negative?std::uint64_t(-std::int32_t(n)):std::uint64_t(n);}
                else if(int64){negative=bool(number>>63);if(negative)number=0-number;}
                else{const auto n=std::bit_cast<std::int32_t>(U32(number));negative=n<0;number=negative?std::uint64_t(-std::int64_t(n)):std::uint64_t(n);}}
            else if(short_value)number&=65535;
            const unsigned base=spec=='o'?8:(spec=='x' || spec=='X' || spec=='p')?16:10;const auto* digits=spec=='x'?"0123456789abcdef":"0123456789ABCDEF";
            if(number || precision!=0)do{value+=digits[number%base];number/=base;}while(number);std::reverse(value.begin(),value.end());
            if(spec=='p')precision=8;if(precision>0 && value.size()<unsigned(precision))value.insert(0,unsigned(precision)-value.size(),'0');
            if(negative)prefix="-";else if(signed_value && plus)prefix="+";else if(signed_value && blank)prefix=" ";
            if(alternate && !value.empty()){if(base==16)prefix+=spec=='x'?"0x":"0X";else if(base==8 && value[0]!='0')prefix+='0';}
        }else throw GuestFault(FaultKind::unsupported,p.cpu.eip,"unsupported wvsprintf conversion");
        const auto length=value.size()+prefix.size();const auto padding=width>0 && unsigned(width)>length?unsigned(width)-length:0;
        if(!left && (!zero || precision>=0))append(std::string(padding,' '));append(prefix);
        if(!left && zero && precision<0)append(std::string(padding,'0'));append(value);if(left)append(std::string(padding,' '));
    }
    return output;
}
}
void install_system(wr::Process& p,ProfileReadHook before_profile_read){
    auto kernel=[&](const std::string& name,unsigned count,std::function<U32(Process&,Args)> fn){bind(p,"kernel32.dll",name,count,std::move(fn));};
    kernel("lstrlenA",1,[](Process& q,Args a){return a[0]?q.string_length(a[0]):0;});
    kernel("lstrcpyA",2,[](Process& q,Args a){string(q,a[0],q.read_string(a[1]));return a[0];});
    kernel("lstrcmpiA",2,[](Process& q,Args a){auto decode=[&](U32 at){const auto text=q.read_string(at);return decode_text(936,{reinterpret_cast<const std::uint8_t*>(text.data()),text.size()});};const auto l=decode(a[0]),r=decode(a[1]);auto fold=[](char16_t c){return character_case(c,false);};
        std::size_t n{};for(;n<std::min(l.size(),r.size());++n)if(fold(l[n])!=fold(r[n]))return U32(fold(l[n])-fold(r[n]));return l.size()==r.size()?0u:l.size()<r.size()?0xffffffffu:1u;});
    kernel("FreeLibrary",1,[](Process& q,Args a){return q.module_name(a[0]).empty()?error(q,6):1;});
    kernel("IsDebuggerPresent",0,[](Process&,Args){return 0u;});
    kernel("GetSystemInfo",1,[](Process& q,Args a){const std::array<U32,9> info{0,4096,65536,0x7ffeffff,1,1,586,65536,6};write(q,a[0],info);return 0u;});
    kernel("GetConsoleCP",0,[](Process&,Args){return 936u;});kernel("GetConsoleOutputCP",0,[](Process&,Args){return 936u;});
    kernel("GetUserDefaultLCID",0,[](Process&,Args){return 0x804u;});
    kernel("GetFullPathNameA",4,[](Process& q,Args a){const auto path=guest_file_path(q,q.read_string(a[0]));auto local=path.lexically_relative(q.options.data_root);
        if(local.empty() || *local.begin()=="..")throw GuestFault(FaultKind::unsupported,q.cpu.eip,"full path outside process data root");
        std::string full="C:\\WinRecomp";if(local!=".")full+='\\'+local.generic_string();std::replace(full.begin(),full.end(),'/','\\');
        if(a[3])q.memory.check(a[3],4,Memory::Write);if(a[1]<=full.size())return U32(full.size()+1);string(q,a[2],full);
        if(a[3])q.memory.store(a[3],a[2]+U32(full.find_last_of('\\')+1),32);return U32(full.size());});
    kernel("SetCurrentDirectoryA",1,[](Process& q,Args a){const auto path=guest_file_path(q,q.read_string(a[0]));const auto info=mounted_resource_info(q,path);std::error_code ec;
        if(!(info && (info->attributes&16)) && !std::filesystem::is_directory(path,ec))return error(q,3);
        const auto relative=path.lexically_relative(q.options.data_root);if(relative.empty() || *relative.begin()=="..")throw GuestFault(FaultKind::unsupported,q.cpu.eip,"working directory outside process data root");q.state().current_directory=info && (info->attributes&16)?info->name:relative=="."?"":relative.generic_string();return 1u;});
    kernel("CopyFileA",3,[](Process& q,Args a){if(!q.options.allow_file_write)return error(q,5);const auto from=guest_file_path(q,q.read_string(a[0])),to=guest_file_path(q,q.read_string(a[1]));std::error_code ec;
        if(mounted_resource_info(q,to))return error(q,5);const auto result=std::filesystem::copy_file(from,to,a[2]?std::filesystem::copy_options::none:std::filesystem::copy_options::overwrite_existing,ec);
        if(!result)return error(q,ec==std::errc::file_exists?80:ec==std::errc::no_such_file_or_directory?2:5);return 1u;});
    kernel("GetPrivateProfileStringA",6,[](Process& q,Args a){if(!a[4])return 0u;if(a[4]>1048576)throw std::runtime_error("profile output limit");
        const auto ini=Profile::read(guest_file_path(q,q.read_string(a[5])));if(!a[0])return output_names(q,a[3],a[4],ini.sections());
        const auto section=q.read_string(a[0]);if(!a[1])return output_names(q,a[3],a[4],ini.keys(section));
        auto value=ini.get(section,q.read_string(a[1]));std::string fallback=a[2]?q.read_string(a[2]):"";while(!fallback.empty() && fallback.back()==' ')fallback.pop_back();
        return output_text(q,a[3],a[4],value?*value:fallback);});
    kernel("GetPrivateProfileIntA",4,[before=std::move(before_profile_read)](Process& q,Args a){if(!a[0] || !a[1])return a[2];
        const auto section=q.read_string(a[0]),key=q.read_string(a[1]);const auto path=guest_file_path(q,q.read_string(a[3]));
        if(before)before(q,path,section,key);auto ini=Profile::read(path);const auto value=ini.get(section,key);return value?profile_integer(*value,a[2]):a[2];});
    kernel("WritePrivateProfileStringA",4,[](Process& q,Args a){if(!q.options.allow_file_write)return error(q,5);if(!a[0] && !a[1] && !a[2])return 1u;if(!a[0] || !a[3])return error(q,87);
        const auto path=guest_file_path(q,q.read_string(a[3]));try{auto ini=Profile::read(path);ini.set(q.read_string(a[0]),nullable_string(q,a[1]),nullable_string(q,a[2]));ini.save(path);return 1u;}catch(const std::filesystem::filesystem_error&){return error(q,5);}});
    for(bool writing:{false,true})kernel(writing?"WritePrivateProfileStructA":"GetPrivateProfileStructA",5,[writing](Process& q,Args a){
        if(writing && !q.options.allow_file_write)return error(q,5);if(!a[0] || !a[1] || a[3]>1048576)return error(q,87);const auto path=guest_file_path(q,q.read_string(a[4]));auto ini=Profile::read(path);
        std::vector<std::uint8_t> bytes(a[3]);if(writing){std::optional<std::string> text;if(a[2]){q.memory.copy_out(a[2],bytes);text=encode_profile_struct(bytes);}ini.set(q.read_string(a[0]),q.read_string(a[1]),std::move(text));ini.save(path);return 1u;}
        if(!a[2])return error(q,87);q.memory.check(a[2],a[3],Memory::Write);const auto text=ini.get(q.read_string(a[0]),q.read_string(a[1]));if(!text || !decode_profile_struct(*text,bytes))return error(q,13);q.memory.copy_in(a[2],bytes);return 1u;
    });
    const auto resources=std::make_shared<Resources>();
    kernel("FindResourceA",3,[resources](Process& q,Args a){if(a[0] && a[0]!=q.image_base)return error(q,6);
        const auto found=find_resource(q.source_image(),resource_name(q,a[2]),resource_name(q,a[1]));if(!found)return error(q,1814);
        for(const auto& [id,item]:resources->items)if(item.resource.rva==found->rva && item.resource.size==found->size)return id;
        const auto id=resources->next;resources->next+=16;resources->items.emplace(id,Resources::Item{*found});return id;});
    kernel("LoadResource",2,[resources](Process& q,Args a){if(a[0] && a[0]!=q.image_base)return error(q,6);const auto it=resources->items.find(a[1]);if(it==resources->items.end())return error(q,1812);
        auto& item=it->second;if(!item.loaded){const auto data=resource_bytes(q.source_image(),item.resource);item.loaded=q.allocate_bytes(data.size());q.memory.copy_in(item.loaded,data);q.memory.protect(item.loaded,(std::max<std::size_t>(1,data.size())+4095)&~std::size_t(4095),Memory::Read);}return item.loaded;});
    kernel("LockResource",1,[resources](Process& q,Args a){for(const auto& [id,item]:resources->items)if(item.loaded && item.loaded==a[0])return a[0];return error(q,6);});
    kernel("SizeofResource",2,[resources](Process& q,Args a){if(a[0] && a[0]!=q.image_base)return error(q,6);const auto it=resources->items.find(a[1]);return it==resources->items.end()?error(q,1812):it->second.resource.size;});
    kernel("FreeResource",1,[](Process&,Args){return 0u;});
    kernel("GetModuleHandleW",1,[](Process& q,Args a){if(!a[0])return q.image_base;std::string name;for(auto c:q.read_wstring(a[0])){if(c>127)return error(q,126);name+=char(c);}return q.module(name);});
    kernel("SetUnhandledExceptionFilter",1,[](Process& q,Args a){if(a[0])q.memory.check(a[0],1,Memory::Execute);const auto previous=q.state().unhandled_filter;q.state().unhandled_filter=a[0];return previous;});
    kernel("InitializeCriticalSectionAndSpinCount",2,[](Process& q,Args a){q.callback(q.resolve("kernel32.dll","InitializeCriticalSection"),std::array<U32,1>{a[0]});q.memory.store(a[0]+20,a[1],32);return 1u;});
    const auto fibers=std::make_shared<Fibers>();const auto seed=wa2::platform::Clock::counter();fibers->cookie=U32(seed)^U32(seed>>32)^0x9e3779b9u;if(!fibers->cookie)fibers->cookie=0xa5b35719;
    kernel("EncodePointer",1,[fibers](Process&,Args a){return std::rotr(a[0]^fibers->cookie,int(fibers->cookie&31));});
    kernel("DecodePointer",1,[fibers](Process&,Args a){return std::rotl(a[0],int(fibers->cookie&31))^fibers->cookie;});
    kernel("FlsAlloc",1,[fibers](Process& q,Args a){if(a[0])q.memory.check(a[0],1,Memory::Execute);for(U32 n=0;n<128;++n)if(!fibers->slots.contains(n)){fibers->slots.emplace(n,Fibers::Slot{a[0],{}});return n;}q.set_error(8);return 0xffffffffu;});
    kernel("FlsGetValue",1,[fibers](Process& q,Args a){const auto it=fibers->slots.find(a[0]);if(it==fibers->slots.end())return error(q,87);q.set_error(0);return it->second.values[q.state().current_thread_id];});
    kernel("FlsSetValue",2,[fibers](Process& q,Args a){const auto it=fibers->slots.find(a[0]);if(it==fibers->slots.end())return error(q,87);it->second.values[q.state().current_thread_id]=a[1];return 1u;});
    kernel("FlsFree",1,[fibers](Process& q,Args a){const auto it=fibers->slots.find(a[0]);if(it==fibers->slots.end())return error(q,87);const auto slot=it->second;it->second.values.clear();
        if(slot.callback)for(const auto& [thread,value]:slot.values)if(value)q.callback(slot.callback,std::array<U32,1>{value});fibers->slots.erase(a[0]);return 1u;});
    p.state().thread_exit.push_back([fibers,&p](U32 thread){for(unsigned pass=0;pass<4;++pass){std::vector<std::array<U32,2>> pending;
        for(auto& [index,slot]:fibers->slots){const auto it=slot.values.find(thread);if(it!=slot.values.end()){const auto value=it->second;slot.values.erase(it);if(value && slot.callback)pending.push_back({slot.callback,value});}}
        if(pending.empty())return;for(const auto& call:pending)p.callback(call[0],std::array<U32,1>{call[1]});}
        throw GuestFault(FaultKind::unsupported,p.cpu.eip,"FLS destructor iteration limit");});
    for(unsigned mode=0;mode<3;++mode)kernel(mode==0?"GetSystemTimeAsFileTime":mode==1?"QueryPerformanceCounter":"QueryPerformanceFrequency",1,[mode](Process& q,Args a){
        const auto value=mode==0?wa2::platform::Clock::file_time():mode==1?wa2::platform::Clock::counter():wa2::platform::Clock::frequency;
        q.memory.check(a[0],8,Memory::Write);q.memory.store(a[0],U32(value),32);q.memory.store(a[0]+4,U32(value>>32),32);return mode?1u:0u;});
    bind(p,"user32.dll","wvsprintfA",3,[](Process& q,Args a){const auto result=format_text(q,a[1],a[2]);string(q,a[0],result);return U32(result.size());});
    p.replace_api("user32.dll","wsprintfA",2,[](Process& q){const auto result=format_text(q,q.argument(1),q.cpu.r[ESP]+12);string(q,q.argument(0),result);return U32(result.size());},true);
    kernel("GetConsoleMode",2,[](Process& q,Args a){const auto it=q.state().files.find(a[0]);if(it==q.state().files.end() || it->second.type!=2)return error(q,6);q.memory.store(a[1],it->second.read?7:3,32);return 1u;});
    for(bool unicode:{false,true})kernel(unicode?"WriteConsoleW":"WriteConsoleA",5,[unicode](Process& q,Args a){if(a[3])q.memory.store(a[3],0,32);const auto it=q.state().files.find(a[0]);if(it==q.state().files.end() || it->second.type!=2 || !it->second.stream || !it->second.write)return error(q,6);
        if(a[4] || a[2]>1048576)return error(q,87);std::u16string wide;
        if(unicode){wide.resize(a[2]);for(U32 n=0;n<a[2];++n)wide[n]=char16_t(q.memory.load(a[1]+n*2,16));}else{std::vector<std::uint8_t> bytes(a[2]);q.memory.copy_out(a[1],bytes);wide=decode_text(936,bytes);}
        const auto utf8=encode_text(65001,wide);if(std::fwrite(utf8.bytes.data(),1,utf8.bytes.size(),it->second.stream)!=utf8.bytes.size())return error(q,29);std::fflush(it->second.stream);if(a[3])q.memory.store(a[3],a[2],32);return 1u;});
    const auto console_handlers=std::make_shared<std::vector<U32>>();
    kernel("SetConsoleCtrlHandler",2,[console_handlers](Process& q,Args a){if(a[0])q.memory.check(a[0],1,Memory::Execute);if(a[1]){console_handlers->push_back(a[0]);return 1u;}
        const auto it=std::find(console_handlers->rbegin(),console_handlers->rend(),a[0]);if(it==console_handlers->rend())return error(q,87);console_handlers->erase(std::next(it).base());return 1u;});
    kernel("FatalAppExitA",2,[](Process& q,Args a)->U32{throw GuestFault(FaultKind::unsupported,q.cpu.eip,"source application terminated: "+q.read_string(a[1]));});
    bind(p,"winmm.dll","timeGetTime",0,[](Process& q,Args){return U32(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-q.state().clock_start).count());});
    const auto periods=std::make_shared<wa2::platform::TimerResolution>();
    for(bool begin:{false,true})bind(p,"winmm.dll",begin?"timeBeginPeriod":"timeEndPeriod",1,[periods,begin](Process&,Args a){if(a[0]<1 || a[0]>1000)return 97u;
        return (begin?periods->begin(a[0]):periods->end(a[0]))?0u:97u;});
    bind(p,"winmm.dll","timeGetDevCaps",2,[](Process& q,Args a){if(a[1]<8)return 97u;write(q,a[0],std::array<U32,2>{1,1000});return 0u;});
    p.state().reports["system_adapter"]=[]{return std::string("{\"backend\":\"portable-cpp\",\"host_windows_apis\":false,\"private_profiles\":true,\"pe_resources\":true}");};
    for(const auto* name:{"EncodePointer","DecodePointer","FlsGetValue","FlsSetValue","lstrlenA","lstrcpyA","lstrcmpiA","IsDebuggerPresent"})p.certify_leaf_api("kernel32.dll",name);
    p.certify_leaf_api("user32.dll","wvsprintfA");p.certify_leaf_api("user32.dll","wsprintfA");
    p.certify_leaf_api("winmm.dll","timeGetTime");
}
}
