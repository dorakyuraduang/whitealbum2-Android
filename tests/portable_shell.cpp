#include "../platform/compat/shell.hpp"
#include "../port/guest_api.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
using namespace wr;
Image fixture(){Image image;image.base=0x400000;image.size=65536;image.headers_size=4096;image.entry=0x401000;image.bytes.resize(8192);image.sections={{".text",4096,4096,4096,4096,0x60000020}};return image;}
int main(int argc,char** argv){try{CHECK(argc==2);const auto root=std::filesystem::path(argv[1]).parent_path()/("shell-fixture-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));CHECK(std::filesystem::create_directory(root));
    const auto selected=root/std::filesystem::path(u"\u5916\u90e8\u76ee\u5f55");CHECK(std::filesystem::create_directory(selected));const auto document=selected/std::filesystem::path(u"\u5b58\u6863.sav");{std::ofstream file(document,std::ios::binary);file<<"original save bytes";}
    ProcessOptions options;options.data_root=root;options.allow_file_write=true;Process p(FunctionExecutor{[](Cpu&,Memory&){return false;}},options);
    unsigned picked{},opened{};wa2::platform::compat::ShellServices services;
    services.pick=[&](const wa2::platform::compat::FileRequest& request)->std::optional<std::filesystem::path>{++picked;return request.directory?selected:document;};
    services.open=[&](std::u16string_view url,std::u16string_view verb){CHECK(url==u"https://example.invalid/help" && verb==u"open");++opened;return true;};wa2::platform::compat::install_shell(p,std::move(services));p.load(fixture());auto& m=p.memory;const auto work=p.allocate_bytes(65536);
    const auto call=[&](const char* dll,const char* name,std::initializer_list<U32> args){return p.callback(p.resolve(dll,name),{args.begin(),args.size()});};
    CHECK(call("ole32.dll","CoInitialize",{0})==0 && call("ole32.dll","CoInitialize",{0})==1);p.state().current_thread_id=2;CHECK(call("ole32.dll","CoInitialize",{0})==0);p.state().current_thread_id=1;
    CHECK(call("shell32.dll","SHGetMalloc",{work})==0);const auto allocator=m.load(work,32);const auto method=[&](unsigned slot,std::initializer_list<U32> args){std::vector<U32> values{allocator};values.insert(values.end(),args.begin(),args.end());return p.callback(m.load(m.load(allocator,32)+slot*4,32),values);};
    const auto block=method(3,{8});m.store(block,0xcafebabe,32);const auto replacement=method(4,{block,8192});CHECK(replacement && m.load(replacement,32)==0xcafebabe && method(7,{block})==0 && method(7,{replacement})==1);CHECK(method(5,{replacement})==0);
    CHECK(call("shell32.dll","SHGetSpecialFolderLocation",{0,5,work+4})==0);const auto pidl=m.load(work+4,32);CHECK(call("shell32.dll","SHGetPathFromIDListA",{pidl,work+1024})==1 && p.read_string(work+1024)=="C:\\WinRecomp\\Documents");CHECK(std::filesystem::is_directory(root/"Documents"));CHECK(method(5,{pidl})==0);
    std::array<U32,19> fields{};fields[0]=76;fields[7]=work+1024;fields[8]=512;m.store(fields[7],0,8);wr::port::write(p,work+128,fields);
    CHECK(call("comdlg32.dll","GetOpenFileNameA",{work+128})==1);const auto alias=p.read_string(work+1024);CHECK(alias.starts_with("C:\\Selected\\") && guest_file_path(p,alias)==document);
    const auto path=p.put_string(alias);const auto file=call("kernel32.dll","CreateFileA",{path,0x80000000,1,0,3,0,0});CHECK(file!=0xffffffffu);CHECK(call("kernel32.dll","ReadFile",{file,work+2048,19,work+4096,0})==1 && m.load(work+4096,32)==19);CHECK(call("kernel32.dll","CloseHandle",{file})==1);
    CHECK(call("comdlg32.dll","GetSaveFileNameA",{work+128})==1);fields[8]=2;m.store(fields[7],0,8);wr::port::write(p,work+128,fields);CHECK(call("comdlg32.dll","GetOpenFileNameA",{work+128})==0 && call("comdlg32.dll","CommDlgExtendedError",{})==0x3003);
    std::array<U32,8> browse{};browse[2]=work+1024;wr::port::write(p,work+128,browse);const auto folder=call("shell32.dll","SHBrowseForFolderA",{work+128});CHECK(folder && call("shell32.dll","SHGetPathFromIDListA",{folder,work+1024})==1);
    const auto folder_alias=p.read_string(work+1024);CHECK(guest_file_path(p,folder_alias+"\\child.txt")==selected/"child.txt");bool escaped=false;try{guest_file_path(p,folder_alias+"\\..\\private.txt");}catch(const GuestFault&){escaped=true;}CHECK(escaped);CHECK(method(5,{folder})==0);
    const auto url=p.put_string("https://example.invalid/help");CHECK(call("shell32.dll","ShellExecuteA",{0,0,url,0,0,1})>32 && opened==1 && picked==4);CHECK(method(2,{})==0);
    CHECK(p.executed_instructions==0);std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.portable-shell.v1\",\"allocator_and_pidl_lifetimes\":true,\"logical_thread_com_initialization\":true,\"unicode_file_selection_uses_ascii_aliases\":true,\"selection_grants_reject_parent_escape\":true,\"short_file_dialog_buffer\":true,\"native_dialog_created\":false,\"external_application_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";std::cout<<"PASS common shell: source allocator/PIDLs, Unicode selections, confined aliases and file dialog layouts with memory callbacks.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
