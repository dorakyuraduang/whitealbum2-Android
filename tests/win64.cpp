#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <io.h>
#include <winrecomp/resources.hpp>
#include <fstream>
#include <iostream>
#include "support.hpp"
#include "selection.hpp"
#include "wa2_win32.hpp"
using namespace wr;
using namespace wr::port;
static unsigned checks=0,locale_visits=0,dialog_visits=0,hook_visits=0,browse_visits=0,handler_visits=0,unwind_visits=0;
#define CHECK(x) do{++checks;if(!(x))throw std::runtime_error(#x);}while(false)
constexpr U32 Locale=0x401000,Dialog=0x401020,Hook=0x401040,Browse=0x401060,Handler=0x401080,Filter=0x4010a0;
static Process* active;static bool modal=true;static U32 ofn_address=0;static std::vector<std::string> locale_names,native_names;
static U32 call(const char* dll,const char* name,std::initializer_list<U32> args){const auto pc=active->resolve(dll,name);CHECK(pc);return active->callback(pc,std::span(args.begin(),args.size()));}
static BOOL CALLBACK native_locale(LPSTR name){native_names.emplace_back(name);return native_names.size()<3;}
static bool step(Cpu& cpu,Memory& memory,std::uint64_t&){
    const auto sp=cpu.r[ESP];U32 result=0,cleanup=0;
    if(cpu.eip==Locale){locale_names.push_back(active->read_string(memory.load(sp+4,32)));++locale_visits;result=locale_visits<3;cleanup=4;}
    else if(cpu.eip==Dialog){const auto window=memory.load(sp+4,32),message=memory.load(sp+8,32);cleanup=16;
        if(message==WM_INITDIALOG){++dialog_visits;CHECK(memory.load(sp+16,32)==0xabcdef12);CHECK(::IsWindow(reinterpret_cast<HWND>(active->gui()->native_window(window))));if(modal)CHECK(call("user32.dll","EndDialog",{window,42}));result=1;}}
    else if(cpu.eip==Hook){cleanup=16;const auto message=memory.load(sp+8,32);if(message==WM_INITDIALOG){++hook_visits;CHECK(memory.load(sp+16,32)==ofn_address);const auto native=reinterpret_cast<HWND>(active->gui()->native_window(memory.load(sp+4,32)));CHECK(::GetWindowThreadProcessId(native,nullptr)==::GetCurrentThreadId());::PostMessageA(::GetAncestor(native,GA_ROOT),WM_CLOSE,0,0);}}
    else if(cpu.eip==Browse){cleanup=16;CHECK(memory.load(sp+16,32)==0xfedcba98);if(memory.load(sp+8,32)==BFFM_INITIALIZED){++browse_visits;const auto native=reinterpret_cast<HWND>(active->gui()->native_window(memory.load(sp+4,32)));::PostMessageA(native,WM_CLOSE,0,0);}}
    else if(cpu.eip==Handler){const auto record=memory.load(sp+4,32),context=memory.load(sp+12,32);const auto flags=memory.load(record+4,32);if(flags&EXCEPTION_UNWINDING){++unwind_visits;result=1;}else{++handler_visits;CHECK(memory.load(record,32)==0xe1234567);CHECK(memory.load(record+16,32)==2);CHECK(memory.load(record+20,32)==0xffffffff);CHECK(memory.load(record+24,32)==0x12345678);memory.store(context+176,0xdeadbeef,32);result=0;}}
    else if(cpu.eip==Filter){cleanup=4;const auto pointers=memory.load(sp+4,32),context=memory.load(pointers+4,32);memory.store(context+176,0xfeed1234,32);result=U32(EXCEPTION_CONTINUE_EXECUTION);}
    else return false;
    cpu.r[EAX]=result;cpu.eip=pop(cpu,memory);cpu.r[ESP]+=cleanup;return true;
}
int main(int argc,char** argv){try{
    CHECK(argc==3);const auto report_path=std::filesystem::absolute(argv[2]);ProcessOptions options;options.instruction_budget=10000000;options.data_root=std::filesystem::absolute(argv[1]).parent_path()/"res";
    Process p(step,options);active=&p;install_wa2_win32(p);const auto image=Image::load(argv[1]);p.load(image);const auto buffer=p.allocate_bytes(4096);
    if(std::string(argv[2])=="--startup-only"){
        STARTUPINFOA native{};::GetStartupInfoA(&native);
        p.memory.store(buffer+68,0xdeadbeef,32);call("kernel32.dll","GetStartupInfoA",{buffer});
        CHECK(p.memory.load(buffer,32)==68 && p.memory.load(buffer+68,32)==0xdeadbeef);
        const std::array<DWORD,8> scalars{native.dwX,native.dwY,native.dwXSize,native.dwYSize,
            native.dwXCountChars,native.dwYCountChars,native.dwFillAttribute,native.dwFlags};
        for(unsigned n=0;n<scalars.size();++n)CHECK(p.memory.load(buffer+16+4*n,32)==scalars[n]);
        CHECK(p.memory.load(buffer+48,16)==native.wShowWindow);
        for(unsigned n=0;n<3;++n)CHECK(p.memory.load(buffer+56+4*n,32)==call("kernel32.dll","GetStdHandle",{U32(STD_INPUT_HANDLE)-n}));
        std::cout<<"PASS "<<checks<<" startup ABI assertions: native show state/scalars retained, 32-bit guest handles, no window or screen access\n";
        return 0;
    }
    unsigned registered=0;for(const auto& item:image.imports)registered+=p.resolve(item.dll,item.ordinal?"#"+std::to_string(*item.ordinal):item.name)!=0;CHECK(registered==image.imports.size());
    std::cerr<<"resources and scalar APIs"<<std::endl;
    const auto entries=resource_directory(image);auto found=std::find_if(entries.begin(),entries.end(),[](const auto& item){return std::holds_alternative<std::uint16_t>(item.type) && std::holds_alternative<std::uint16_t>(item.name);});CHECK(found!=entries.end());
    const auto resource=call("kernel32.dll","FindResourceA",{p.image_base,std::get<std::uint16_t>(found->name),std::get<std::uint16_t>(found->type)});CHECK(resource);
    const auto loaded=call("kernel32.dll","LoadResource",{p.image_base,resource});CHECK(loaded && call("kernel32.dll","LockResource",{loaded})==loaded);
    const auto expected=resource_bytes(image,*found);std::vector<std::uint8_t> bytes(expected.size());p.memory.copy_out(loaded,bytes);CHECK(std::equal(bytes.begin(),bytes.end(),expected.begin()));CHECK(!(p.memory.permissions(loaded)&Memory::Write));CHECK(call("kernel32.dll","FreeResource",{loaded})==0);CHECK(call("kernel32.dll","LockResource",{loaded})==loaded);
    SYSTEMTIME time{2024,2,4,29,23,45,56,123};write(p,buffer,time);
    for(bool date:{false,true}){const auto format=p.put_string(date?"yyyy-MM-dd":"HH:mm:ss");std::array<char,128> native{};
        const auto count=date?::GetDateFormatA(LOCALE_USER_DEFAULT,0,&time,date?"yyyy-MM-dd":nullptr,native.data(),int(native.size())): ::GetTimeFormatA(LOCALE_USER_DEFAULT,0,&time,"HH:mm:ss",native.data(),int(native.size()));
        CHECK(call("kernel32.dll",date?"GetDateFormatA":"GetTimeFormatA",{LOCALE_USER_DEFAULT,0,buffer,format,buffer+128,128})==U32(count));CHECK(p.read_string(buffer+128)==native.data());
        CHECK(call("kernel32.dll",date?"GetDateFormatA":"GetTimeFormatA",{LOCALE_USER_DEFAULT,0,buffer,format,0,0})==U32(count));}
    ::EnumSystemLocalesA(native_locale,LCID_INSTALLED);CHECK(call("kernel32.dll","EnumSystemLocalesA",{Locale,LCID_INSTALLED}));CHECK(locale_names==native_names && locale_visits==3);
    CHECK(!call("kernel32.dll","WriteConsoleA",{0,buffer,1,buffer+512,0}) && p.last_error==ERROR_INVALID_HANDLE);CHECK(p.memory.load(buffer+512,32)==0);
    CHECK(!call("kernel32.dll","WriteConsoleW",{0,buffer,1,buffer+512,0}) && p.last_error==ERROR_INVALID_HANDLE);
    for(const auto [native_stream,id]:{std::pair{stdin,U32(STD_INPUT_HANDLE)},std::pair{stdout,U32(STD_OUTPUT_HANDLE)},std::pair{stderr,U32(STD_ERROR_HANDLE)}}){
        DWORD mode{};const auto handle=reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(native_stream)));const auto result=::GetConsoleMode(handle,&mode);const auto error=::GetLastError();
        const auto guest=call("kernel32.dll","GetStdHandle",{id});p.memory.store(buffer+512,0xa5a5a5a5,32);CHECK(call("kernel32.dll","GetConsoleMode",{guest,buffer+512})==U32(result));
        if(result)CHECK(p.memory.load(buffer+512,32)==mode);else{CHECK(p.last_error==error);CHECK(p.memory.load(buffer+512,32)==0xa5a5a5a5);}
    }
    CHECK(call("kernel32.dll","SetConsoleCtrlHandler",{Handler,TRUE}));CHECK(call("kernel32.dll","SetConsoleCtrlHandler",{Handler,FALSE}));CHECK(!call("kernel32.dll","SetConsoleCtrlHandler",{Handler,FALSE}) && p.last_error==ERROR_INVALID_PARAMETER);
    std::cerr<<"modal/modeless native dialogs"<<std::endl;
    CHECK(call("user32.dll","DialogBoxParamA",{p.image_base,104,0,Dialog,0xabcdef12})==42);CHECK(dialog_visits==1);
    modal=false;const auto dialog=call("user32.dll","CreateDialogParamA",{p.image_base,104,0,Dialog,0xabcdef12});CHECK(dialog && dialog_visits==2);
    const auto native_dialog=reinterpret_cast<HWND>(p.gui()->native_window(dialog));HWND native_control=::GetWindow(native_dialog,GW_CHILD);CHECK(native_control);int id=::GetDlgCtrlID(native_control);
    while(id==-1 && native_control){native_control=::GetWindow(native_control,GW_HWNDNEXT);if(native_control)id=::GetDlgCtrlID(native_control);}CHECK(native_control);
    const auto control=call("user32.dll","GetDlgItem",{dialog,U32(id)});CHECK(control && p.gui()->native_window(control)==reinterpret_cast<std::uintptr_t>(native_control));CHECK(call("user32.dll","IsWindow",{control}));
    CHECK(call("user32.dll","GetWindowLongA",{control,U32(GWLP_ID)})==U32(id));
    const auto text=p.put_string("Win64 bridge");CHECK(call("user32.dll","SetDlgItemTextA",{dialog,U32(id),text}));CHECK(call("user32.dll","SendDlgItemMessageA",{dialog,U32(id),WM_GETTEXT,128,buffer+256})==12);CHECK(p.read_string(buffer+256)=="Win64 bridge");CHECK(call("user32.dll","DestroyWindow",{dialog}));
    ofn_address=buffer+1024;std::array<U32,22> ofn{};ofn[0]=88;ofn[7]=p.allocate_bytes(4096);ofn[8]=260;ofn[13]=OFN_EXPLORER|OFN_ENABLEHOOK|OFN_NOCHANGEDIR;ofn[17]=Hook;write(p,ofn_address,ofn);
    std::cerr<<"native open-file dialog"<<std::endl;
    CHECK(!call("comdlg32.dll","GetOpenFileNameA",{ofn_address}) && p.last_error==0 && hook_visits==1);
    std::cerr<<"native save-file dialog"<<std::endl;
    write(p,ofn_address,ofn);CHECK(!call("comdlg32.dll","GetSaveFileNameA",{ofn_address}) && p.last_error==0 && hook_visits==2);
    std::cerr<<"native folder dialog"<<std::endl;
    call("ole32.dll","CoInitialize",{0});const std::array<U32,8> browse{0,0,buffer+256,0,BIF_RETURNONLYFSDIRS,Browse,0xfedcba98,0};write(p,buffer+512,browse);CHECK(call("shell32.dll","SHBrowseForFolderA",{buffer+512})==0 && browse_visits==1);call("ole32.dll","CoUninitialize",{});
    CHECK(call("shell32.dll","ShellExecuteA",{0,0,p.put_string("definitely-missing-wa2-bridge-test.exe"),0,0,SW_HIDE})<=32);
    std::cerr<<"selected native paths and current directory"<<std::endl;
    const auto selection=report_path.parent_path().parent_path()/"build/win64-selected";std::filesystem::create_directories(selection/"folder");
    const auto exact=selection/"exact.bin",nested=selection/"folder/nested.bin";
    {std::ofstream(exact)<<"selected";std::ofstream(nested)<<"nested";}
    auto refused=[&](const std::filesystem::path& name){try{guest_file_path(p,name.string());return false;}catch(const GuestFault&){return true;}};
    CHECK(refused(exact));selected_path(p,exact,false);CHECK(guest_file_path(p,exact.string())==std::filesystem::canonical(exact));CHECK(refused(selection/"other.bin"));
    selected_path(p,selection/"folder",true);CHECK(guest_file_path(p,nested.string())==std::filesystem::canonical(nested));CHECK(refused(selection/"folder/../other.bin"));
    const auto previous=p.state().current_directory;CHECK(call("kernel32.dll","SetCurrentDirectoryA",{p.put_string((selection/"folder").string())}));CHECK(guest_file_path(p,"nested.bin")==std::filesystem::canonical(nested));CHECK(call("kernel32.dll","GetCurrentDirectoryA",{1024,buffer+256}));CHECK(std::filesystem::path(p.read_string(buffer+256))==std::filesystem::canonical(selection/"folder"));p.state().current_directory=previous;
    std::cerr<<"guest exception record/context dispatch"<<std::endl;
    const auto frame=p.cpu.r[ESP]-512;write(p,frame,std::array<U32,2>{0xffffffffu,Handler});p.memory.store(p.cpu.fs_base,frame,32);write(p,buffer,std::array<U32,2>{0xffffffffu,0x12345678});
    CHECK(call("kernel32.dll","RaiseException",{0xe1234567,0,2,buffer})==0xdeadbeef && handler_visits==1);
    const auto second=frame+64;write(p,frame,std::array<U32,2>{second,Handler});write(p,second,std::array<U32,2>{0xffffffffu,Handler});p.memory.store(p.cpu.fs_base,frame,32);
    CHECK(call("kernel32.dll","RtlUnwind",{second,0,0,0x13572468})==0x13572468);CHECK(unwind_visits==1 && p.memory.load(p.cpu.fs_base,32)==second);
    p.memory.store(p.cpu.fs_base,0xffffffffu,32);CHECK(call("kernel32.dll","SetUnhandledExceptionFilter",{Filter})==0);
    CHECK(call("kernel32.dll","RaiseException",{0xe1234567,0,2,buffer})==0xfeed1234);CHECK(call("kernel32.dll","SetUnhandledExceptionFilter",{0})==Filter);
    std::ofstream report(report_path);report<<"{\"schema\":\"wa2.win64-bridge-tests.v1\",\"assertions\":"<<checks<<",\"registered_imports\":"<<registered<<",\"total_imports\":"<<image.imports.size()<<",\"native_pointer_bits\":64,\"locale_callbacks\":"<<locale_visits<<",\"native_dialogs\":"<<dialog_visits<<",\"native_file_dialog_cancellations\":"<<hook_visits<<",\"native_folder_dialog_cancellations\":"<<browse_visits<<",\"seh_handlers\":"<<handler_visits<<",\"unwind_handlers\":"<<unwind_visits<<",\"full_api_conformance\":false}\n";CHECK(report);
    std::cout<<"PASS "<<checks<<" Win64 bridge assertions, "<<registered<<'/'<<image.imports.size()<<" imports, real native dialogs/pickers, resources, dates/locales, console and guest SEH\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
