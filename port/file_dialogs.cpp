#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <winrecomp/resources.hpp>
#include "selection.hpp"
namespace wr { namespace {
using namespace port;
std::string filter_string(Process& p,U32 at){if(!at)return {};std::string value;for(unsigned n=0;n<65536;++n){const auto ch=char(p.memory.load(at+n,8));value+=ch;if(!ch && n && !value[n-1])return value;}throw GuestFault(FaultKind::unsupported,p.cpu.eip,"unterminated file-dialog filter");}
struct TemplateMemory {HGLOBAL value{};~TemplateMemory(){if(value)::GlobalFree(value);}};
struct FileCall {
    Process& p;U32 guest{},hook{};std::array<U32,22> fields{};OPENFILENAMEA native{};
    std::vector<char> file,title,custom;std::string filter,initial,caption,extension;TemplateMemory memory;std::exception_ptr failure;
    void outputs(){
        auto copy=[&](U32 at,const std::vector<char>& bytes){if(at && !bytes.empty())p.memory.copy_in(at,std::span(reinterpret_cast<const std::uint8_t*>(bytes.data()),bytes.size()));};
        copy(fields[7],file);copy(fields[9],title);copy(fields[4],custom);
        constexpr DWORD changed=OFN_NOCHANGEDIR|OFN_ENABLETEMPLATE|OFN_ENABLETEMPLATEHANDLE;
        p.memory.store(guest+24,native.nFilterIndex,32);p.memory.store(guest+52,(native.Flags&~changed)|(fields[13]&changed),32);
        p.memory.store(guest+56,native.nFileOffset,16);p.memory.store(guest+58,native.nFileExtension,16);
    }
    void inputs(){
        p.memory.copy_out(fields[7],std::span(reinterpret_cast<std::uint8_t*>(file.data()),file.size()));
        native.nFilterIndex=p.memory.load(guest+24,32);native.lCustData=LONG_PTR(std::bit_cast<std::int32_t>(p.memory.load(guest+64,32)));
    }
};
thread_local FileCall* current_file{};
UINT_PTR CALLBACK file_hook(HWND hwnd,UINT message,WPARAM wp,LPARAM lp) noexcept {
    auto& call=*current_file;U32 scratch=0;
    try{
        auto& p=call.p;call.outputs();const auto window=p.gui()->native_window_token(reinterpret_cast<std::uintptr_t>(hwnd));U32 first=U32(wp),second=U32(lp);
        if(message==WM_INITDIALOG){first=p.gui()->native_window_token(wp);second=call.guest;}
        else if(message==WM_NOTIFY){
            const auto* notice=reinterpret_cast<const OFNOTIFYA*>(lp);if(!notice)return 0;
            if(notice->hdr.code==CDN_INCLUDEITEM)throw GuestFault(FaultKind::unsupported,p.cpu.eip,"shell-item include callback needs a shell COM bridge");
            scratch=p.allocate_bytes(65536);p.memory.store(scratch,p.gui()->native_window_token(reinterpret_cast<std::uintptr_t>(notice->hdr.hwndFrom)),32);
            if(notice->hdr.idFrom>0xffffffffull)throw GuestFault(FaultKind::unsupported,p.cpu.eip,"file-dialog control ID exceeds guest width");
            p.memory.store(scratch+4,U32(notice->hdr.idFrom),32);p.memory.store(scratch+8,notice->hdr.code,32);p.memory.store(scratch+12,call.guest,32);
            U32 name=0;if(notice->pszFile){const auto length=strnlen(notice->pszFile,32768);if(length==32768)throw std::runtime_error("file notification path limit");name=scratch+32;string(p,name,std::string(notice->pszFile,length));}p.memory.store(scratch+16,name,32);second=scratch;
        }else{const auto result=p.gui()->native_callback(call.hook,reinterpret_cast<std::uintptr_t>(hwnd),message,wp,lp);call.inputs();return result;}
        const std::array<U32,4> args{window,message,first,second};const auto result=p.callback(call.hook,args);
        call.inputs();if(scratch)p.memory.release(scratch);return result;
    }catch(...){if(scratch)try{call.p.memory.release(scratch);}catch(...){}call.failure=std::current_exception();::PostMessageA(::GetAncestor(hwnd,GA_ROOT),WM_CLOSE,0,0);return 0;}
}
U32 choose_file(Process& p,U32 at,bool save){
    const auto size=p.memory.load(at,32);if(size!=76 && size!=88){p.set_error(ERROR_INVALID_PARAMETER);return 0;}p.memory.check(at,size,Memory::Read|Memory::Write);
    FileCall call{p};call.guest=at;for(unsigned n=0;n<size/4;++n)call.fields[n]=p.memory.load(at+n*4,32);auto& a=call.fields;auto& native=call.native;
    if(size==88 && (a[19] || a[20])){p.set_error(ERROR_INVALID_PARAMETER);return 0;}
    if(!a[7] || !a[8] || a[8]>1048576 || a[5]>65536 || a[10]>65536){p.set_error(ERROR_INVALID_PARAMETER);return 0;}
    auto buffer=[&](U32 pointer,U32 count,std::vector<char>& bytes){if(!pointer)return;if(count){p.memory.check(pointer,count,Memory::Read|Memory::Write);bytes.resize(count);p.memory.copy_out(pointer,std::span(reinterpret_cast<std::uint8_t*>(bytes.data()),count));}};
    buffer(a[7],a[8],call.file);buffer(a[9],a[10],call.title);buffer(a[4],a[5],call.custom);call.filter=filter_string(p,a[3]);
    call.initial=a[11]?guest_file_path(p,p.read_string(a[11])).string():p.options.data_root.string();call.caption=a[12]?p.read_string(a[12]):"";call.extension=a[15]?p.read_string(a[15]):"";
    native.lStructSize=sizeof(native);native.hwndOwner=reinterpret_cast<HWND>(p.gui()->native_window(a[1]));native.lpstrFilter=a[3]?call.filter.c_str():nullptr;
    native.lpstrCustomFilter=a[4]?call.custom.data():nullptr;native.nMaxCustFilter=a[5];native.nFilterIndex=a[6];native.lpstrFile=call.file.data();native.nMaxFile=a[8];native.lpstrFileTitle=a[9]?call.title.data():nullptr;native.nMaxFileTitle=a[10];native.lpstrInitialDir=call.initial.c_str();native.lpstrTitle=a[12]?call.caption.c_str():nullptr;native.Flags=a[13]|OFN_NOCHANGEDIR;native.lpstrDefExt=a[15]?call.extension.c_str():nullptr;native.lCustData=LONG_PTR(std::bit_cast<std::int32_t>(a[16]));native.FlagsEx=size==88?a[21]:0;
    if(native.Flags&OFN_ENABLEHOOK){p.memory.check(a[17],1,Memory::Execute);call.hook=a[17];native.lpfnHook=file_hook;}
    if(native.Flags&(OFN_ENABLETEMPLATE|OFN_ENABLETEMPLATEHANDLE)){
        std::vector<std::uint8_t> bytes;
        if(native.Flags&OFN_ENABLETEMPLATEHANDLE){const auto region=p.memory.allocation(a[2]);const auto count=std::min<std::size_t>(region.size-(a[2]-region.base),1048576);bytes.resize(count);p.memory.copy_out(a[2],bytes);}
        else{ResourceName name=std::uint16_t(a[18]);if(a[18]>=65536){const auto text=p.read_string(a[18]);std::u16string wide;for(unsigned char ch:text)wide+=char16_t(ch);name=wide;}const auto resource=find_resource(p.source_image(),std::uint16_t(5),name);if(!resource){p.set_error(ERROR_RESOURCE_NAME_NOT_FOUND);return 0;}const auto data=resource_bytes(p.source_image(),*resource);bytes.assign(data.begin(),data.end());}
        call.memory.value=::GlobalAlloc(GMEM_FIXED,bytes.size());if(!call.memory.value){p.set_error(ERROR_NOT_ENOUGH_MEMORY);return 0;}std::memcpy(call.memory.value,bytes.data(),bytes.size());native.hInstance=reinterpret_cast<HINSTANCE>(call.memory.value);native.Flags=(native.Flags&~OFN_ENABLETEMPLATE)|OFN_ENABLETEMPLATEHANDLE;
    }
    const auto prior=current_file;current_file=&call;const auto result=save?::GetSaveFileNameA(&native): ::GetOpenFileNameA(&native);const auto error=::CommDlgExtendedError();current_file=prior;call.outputs();
    if(call.failure)std::rethrow_exception(call.failure);if(!result){p.set_error(error);return 0;}
    const auto length=strnlen(call.file.data(),call.file.size());if(length==call.file.size())throw std::runtime_error("unterminated selected path");const auto first=std::string(call.file.data(),length);const auto next=first.size()+1;
    if((native.Flags&OFN_ALLOWMULTISELECT) && next<call.file.size() && call.file[next]){for(std::size_t offset=next;offset<call.file.size() && call.file[offset];){const auto count=strnlen(call.file.data()+offset,call.file.size()-offset);if(count==call.file.size()-offset)throw std::runtime_error("unterminated file selection");selected_path(p,std::filesystem::path(first)/std::string(call.file.data()+offset,count),false);offset+=count+1;}}
    else selected_path(p,first,false);return 1;
}
}
void install_wa2_file_dialogs(Process& p){for(bool save:{false,true})api(p,"comdlg32.dll",save?"GetSaveFileNameA":"GetOpenFileNameA",1,[save](Process& q,Args a){return choose_file(q,a[0],save);});}
}
