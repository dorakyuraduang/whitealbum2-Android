#include "shell.hpp"
#include "codepage.hpp"
#include "../../port/guest_api.hpp"
#include "../../generated/graphics-abi/source_abi.hpp"
#include <algorithm>
#include <cctype>
#include <map>
namespace wa2::platform::compat {namespace {
using namespace wr;using namespace wr::port;using graphics::abi::GUID;
std::u16string wide(Process& p,U32 at){if(!at)return {};const auto text=p.read_string(at);return decode_text(936,std::span(reinterpret_cast<const std::uint8_t*>(text.data()),text.size()));}
std::string narrow(std::u16string_view value){const auto bytes=encode_text(936,value).bytes;return std::string(bytes.begin(),bytes.end());}
class Shell {
    Process& p;ShellServices services;std::map<U32,U32> blocks;std::map<U32,std::string> pidls;std::map<U32,unsigned> com_threads;
    U32 allocator{},references{},dialog_error{},next_selection{};std::array<U32,9> methods{};
    [[noreturn]]void unsupported(const std::string& value){throw GuestFault(FaultKind::unsupported,p.cpu.eip,"shared shell: "+value);}
    U32 allocate(U32 bytes){if(bytes>128u*1024*1024)return 0;const auto at=p.allocate_bytes(std::max(bytes,1u));blocks.emplace(at,bytes);return at;}
    void free(U32 at){if(!at)return;if(!blocks.contains(at))unsupported("foreign allocator pointer");p.memory.release(at);blocks.erase(at);pidls.erase(at);}
    U32 reallocate(U32 at,U32 size){if(!at)return allocate(size);if(!blocks.contains(at))unsupported("foreign realloc pointer");if(!size){free(at);return 0;}const auto replacement=allocate(size);if(!replacement)return 0;
        try{std::vector<std::uint8_t> bytes(std::min(blocks.at(at),size));p.memory.copy_out(at,bytes);p.memory.copy_in(replacement,bytes);}catch(...){free(replacement);throw;}free(at);return replacement;}
    U32 pidl(const std::string& value){if(value.size()>32760)unsupported("PIDL path limit");const auto size=U32(value.size()+4),at=allocate(size+2);p.memory.store(at,size,16);p.memory.store(at+2,0xa2,8);string(p,at+3,value);p.memory.store(at+size,0,16);pidls.emplace(at,value);return at;}
    std::string selected(const std::filesystem::path& path,bool directory){const auto actual=std::filesystem::weakly_canonical(path);
        std::string alias="C:/Selected/"+std::to_string(++next_selection);if(!directory){alias+="/file";const auto extension=actual.extension().u8string();if(extension.size()<16 && std::all_of(extension.begin(),extension.end(),[](auto c){return c=='.' || (c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9');}))alias.append(extension.begin(),extension.end());}
        auto key=alias;for(auto& c:key)c=char(std::tolower(static_cast<unsigned char>(c)));p.state().selected_paths[key]={actual,directory};std::replace(alias.begin(),alias.end(),'/','\\');return alias;}
    U32 file_dialog(Args args,bool save){dialog_error=0;const auto at=args[0],size=p.memory.load(at,32);if(size!=76 && size!=88){dialog_error=1;return 0;}p.memory.check(at,size,Memory::Read|Memory::Write);const auto fields=read<std::array<U32,19>>(p,at);
        if(fields[13]&(0x20|0x40|0x80|0x200))unsupported("file-dialog hooks/templates or multi-selection");if(!fields[7] || !fields[8] || fields[8]>1048576){dialog_error=2;return 0;}
        p.memory.check(fields[7],fields[8],Memory::Read|Memory::Write);FileRequest request;request.save=save;request.owner=fields[1];request.flags=fields[13];request.title=wide(p,fields[12]);request.extension=wide(p,fields[15]);
        {const auto value=p.read_string(fields[7],fields[8]);request.suggested_filename=decode_text(936,std::span(reinterpret_cast<const std::uint8_t*>(value.data()),value.size()));}
        if(fields[11])request.initial=guest_file_path(p,p.read_string(fields[11]));else request.initial=p.options.data_root;
        if(fields[3]){U32 cursor=fields[3];for(unsigned n=0;n<64;++n){const auto label=p.read_string(cursor,32768);if(label.empty())break;cursor+=U32(label.size()+1);const auto pattern=p.read_string(cursor,32768);if(pattern.empty())unsupported("empty filename pattern");cursor+=U32(pattern.size()+1);request.filters.push_back({decode_text(936,std::span(reinterpret_cast<const std::uint8_t*>(label.data()),label.size())),decode_text(936,std::span(reinterpret_cast<const std::uint8_t*>(pattern.data()),pattern.size()))});}}
        if(!services.pick)unsupported("no native file chooser");const auto result=services.pick(request);if(!result)return 0;const auto path=selected(*result,false);
        if(path.size()+1>fields[8]){if(fields[8]>=2)p.memory.store(fields[7],std::min<std::size_t>(path.size()+1,65535),16);dialog_error=0x3003;return 0;}
        string(p,fields[7],path);const auto filename=narrow(result->filename().u16string());const auto offset=path.find_last_of("/\\");const auto dot=path.find_last_of('.');const auto file_offset=offset==std::string::npos?0:offset+1;
        p.memory.store(at+56,U32(file_offset),16);p.memory.store(at+58,dot==std::string::npos || dot<file_offset?0:U32(dot+1),16);
        if(fields[9] && fields[10]){const auto count=std::min<std::size_t>(filename.size(),fields[10]-1);p.memory.check(fields[9],count+1,Memory::Write);string(p,fields[9],filename.substr(0,count));}
        return 1;
    }
public:
    Shell(Process& process,ShellServices value):p(process),services(std::move(value)){}
    void shutdown()noexcept{for(auto [at,size]:blocks)try{p.memory.release(at);}catch(...){}blocks.clear();pidls.clear();}
    void install(){
        constexpr std::array<unsigned,9> counts{3,1,1,2,3,2,2,2,1};
        for(unsigned slot=0;slot<9;++slot){const auto name="Malloc."+std::to_string(slot);api(p,"wa2.shell",name,counts[slot],[this,slot](Process&,Args a){if(a[0]!=allocator || !references)unsupported("invalid allocator receiver");switch(slot){
            case 0:{p.memory.check(a[2],4,Memory::Write);p.memory.store(a[2],0,32);const auto iid=read<GUID>(p,a[1]);constexpr GUID malloc_iid{2,0,0,{0xc0,0,0,0,0,0,0,0x46}};if(iid!=malloc_iid && iid!=graphics::abi::IID_IUnknown)return U32(0x80004002);++references;p.memory.store(a[2],allocator,32);return U32(0);}
            case 1:return ++references;case 2:return --references;case 3:return allocate(a[1]);case 4:return reallocate(a[1],a[2]);case 5:free(a[1]);return U32(0);case 6:return a[1] && blocks.contains(a[1])?U32(p.memory.allocation(a[1]).size):0xffffffffu;case 7:return a[1]?U32(blocks.contains(a[1])):0xffffffffu;case 8:return U32(0);}
            unsupported("unknown allocator method");});methods[slot]=p.resolve("wa2.shell",name);}
        api(p,"shell32.dll","SHGetMalloc",1,[this](Process&,Args a){if(!a[0])return U32(0x80004003);p.memory.check(a[0],4,Memory::Write);if(!allocator){const auto table=p.allocate_bytes(36);write(p,table,methods);p.memory.protect(table,4096,Memory::Read);allocator=p.allocate_bytes(4);p.memory.store(allocator,table,32);p.memory.protect(allocator,4096,Memory::Read);}++references;p.memory.store(a[0],allocator,32);return U32(0);});
        api(p,"shell32.dll","SHGetSpecialFolderLocation",3,[this](Process&,Args a){p.memory.check(a[2],4,Memory::Write);p.memory.store(a[2],0,32);std::string folder="C:\\WinRecomp";switch(a[1]&255){case 0:case 0x10:folder+="\\Desktop";break;case 5:folder+="\\Documents";break;case 0x1a:folder+="\\AppData\\Roaming";break;case 0x1c:folder+="\\AppData\\Local";break;case 0x23:folder+="\\ProgramData";break;case 0x28:break;default:return U32(0x80070057);}
            if(p.options.allow_file_write){std::error_code error;std::filesystem::create_directories(guest_file_path(p,folder),error);if(error)return U32(0x80070003);}p.memory.store(a[2],pidl(folder),32);return U32(0);});
        api(p,"shell32.dll","SHGetPathFromIDListA",2,[this](Process&,Args a){const auto found=pidls.find(a[0]);if(found==pidls.end() || found->second.size()>=260 || !a[1])return U32(0);string(p,a[1],found->second);return U32(1);});
        api(p,"shell32.dll","SHBrowseForFolderA",1,[this](Process&,Args a){const auto fields=read<std::array<U32,8>>(p,a[0]);FileRequest request;request.directory=true;request.owner=fields[0];request.flags=fields[4];request.title=wide(p,fields[3]);request.initial=p.options.data_root;
            if(fields[1]){const auto root=pidls.find(fields[1]);if(root==pidls.end())unsupported("unknown folder browser root");request.initial=guest_file_path(p,root->second);}if(!services.pick)unsupported("no native folder chooser");
            U32 dialog{};bool accepted=true;auto cleanup=[&]{if(dialog){p.gui()->detach_system_dialog(dialog);dialog=0;}};
            try{if(fields[5]){if(!p.gui())unsupported("folder browser has no GUI adapter");dialog=p.gui()->attach_system_dialog([&](U32 message,U32 wp,U32 lp){
                        if(message==0x466){if(wp)request.initial=guest_file_path(p,p.read_string(lp));else{const auto found=pidls.find(lp);if(found==pidls.end())unsupported("unknown browser-selection PIDL");request.initial=guest_file_path(p,found->second);}return U32(0);}
                        if(message==0x465){accepted=lp!=0;return U32(0);}if(message==0x464){request.title=wide(p,lp);return U32(0);}unsupported("folder browser message "+std::to_string(message));
                    });p.callback(fields[5],std::array<U32,4>{dialog,1,0,fields[6]});}
                for(;;){const auto result=services.pick(request);if(!result){cleanup();return U32(0);}const auto path=selected(*result,true);const auto id=pidl(path);
                    if(fields[5])p.callback(fields[5],std::array<U32,4>{dialog,2,id,fields[6]});if(!accepted){free(id);request.initial=*result;continue;}
                    if(fields[2]){const auto filename=narrow(result->filename().u16string());string(p,fields[2],filename.substr(0,259));}p.memory.store(a[0]+28,0,32);cleanup();return id;}
            }catch(...){cleanup();throw;}});
        api(p,"shell32.dll","ShellExecuteA",6,[this](Process&,Args a){if(!a[2])return U32(2);if(a[3] && !p.read_string(a[3]).empty())unsupported("external command parameters");auto target=wide(p,a[2]);const auto verb=a[1]?wide(p,a[1]):u"open";const auto bytes=narrow(target);
            if(bytes.find("://")==std::string::npos && !bytes.starts_with("mailto:")){const auto path=guest_file_path(p,bytes);auto extension=path.extension().string();for(auto& c:extension)c=char(std::tolower(static_cast<unsigned char>(c)));if(extension==".exe" || extension==".cmd" || extension==".bat")unsupported("external executable dependency");target=path.u16string();}
            return services.open && services.open(target,verb)?U32(0xbb000000):U32(31);});
        api(p,"ole32.dll","CoInitialize",1,[this](Process&,Args a){if(a[0])return U32(0x80070057);auto& count=com_threads[p.state().current_thread_id];return count++?U32(1):U32(0);});
        api(p,"ole32.dll","CoUninitialize",0,[this](Process&,Args){auto& count=com_threads[p.state().current_thread_id];if(count)--count;return U32(0);});
        api(p,"ole32.dll","CoTaskMemAlloc",1,[this](Process&,Args a){return allocate(a[0]);});api(p,"ole32.dll","CoTaskMemRealloc",2,[this](Process&,Args a){return reallocate(a[0],a[1]);});api(p,"ole32.dll","CoTaskMemFree",1,[this](Process&,Args a){free(a[0]);return U32(0);});
        api(p,"comdlg32.dll","GetOpenFileNameA",1,[this](Process&,Args a){return file_dialog(a,false);});api(p,"comdlg32.dll","GetSaveFileNameA",1,[this](Process&,Args a){return file_dialog(a,true);});
        api(p,"comdlg32.dll","CommDlgExtendedError",0,[this](Process&,Args){return dialog_error;});
    }
};
}
void install_shell(wr::Process& p,ShellServices services){auto state=std::make_shared<Shell>(p,std::move(services));state->install();p.state().shutdown.push_back([state]{state->shutdown();});}
}
