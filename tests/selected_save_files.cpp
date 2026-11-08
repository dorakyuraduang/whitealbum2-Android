#include "../platform/compat/save_files.hpp"
#include "../platform/compat/resource_mount.hpp"
#include "../platform/compat/system.hpp"
#include "../platform/compat/profile.hpp"
#include "../engine/test_progress.hpp"
#include <winrecomp/process.hpp>
#include <winrecomp/win32_state.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
using namespace wr;namespace c=wa2::platform::compat;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
Image fixture(){Image image;image.base=0x400000;image.entry=0x401000;image.size=0x10000;image.headers_size=4096;image.bytes.resize(8192);image.sections={{".text",4096,4096,4096,4096,0x60000020}};return image;}
std::vector<std::uint8_t> read(const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);CHECK(in);return {std::istreambuf_iterator<char>(in),{}};}
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{};const auto parent=std::filesystem::canonical(std::filesystem::path(argv[1]).parent_path());const auto root=parent/("selected-save-fixture-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));CHECK(std::filesystem::create_directory(root));
    const auto selected=root/"Wa2Res",private_root=root/"private";CHECK(std::filesystem::create_directory(selected) && std::filesystem::create_directory(private_root));
    std::ofstream(selected/"Sys.sav",std::ios::binary)<<"PC-SAVE";std::ofstream(private_root/"Sys.sav")<<"WRONG";
    std::ofstream(selected/"SYSTEM.ini")<<"[DEFAULT]\r\nbgm_vol=37\r\nmov_lv=2\r\nunknown=keep\r\n";
    std::ofstream(selected/"default.ini")<<"[DEFAULT]\r\nvalue=17\r\n";
    auto files=c::save_files(c::local_files(selected),false);ProcessOptions options;options.data_root=private_root;options.allow_file_write=true;
    Process p(FunctionExecutor{[](Cpu&,Memory&)->bool{throw std::runtime_error("File checks must never execute game code");}},options);c::install_system(p);p.state().mutable_files=files;
    c::install_resource_mount(p,std::make_shared<c::ResourceMount>(std::vector<ReadOnlyFileInfo>{{"grp.pak",4,0},{"IC/grp.pak",4,0}},[](std::string_view)->std::FILE*{throw std::runtime_error("PAK payloads must not be opened");}));p.load(fixture());const auto memory=p.allocate_bytes(4096);const auto base_handles=p.state().files.size();
    const auto call=[&](const char* name,std::initializer_list<U32> arguments){const auto target=p.resolve("kernel32.dll",name);CHECK(target);return p.callback(target,{arguments.begin(),arguments.size()});};
    const auto primary=p.put_string("Sys.sav"),alias=p.put_string("C:\\WinRecomp\\Documents\\Leaf\\WHITE ALBUM2\\Sys.sav");
    auto handle=call("CreateFileA",{alias,0x80000000,1,0,3,128,0});CHECK(handle!=0xffffffff && call("GetFileSize",{handle,0})==7);++cases;
    CHECK(call("ReadFile",{handle,memory,7,memory+128,0})==1 && p.read_string(memory,8)=="PC-SAVE");++cases;
    CHECK(call("CreateFileA",{primary,0x40000000,3,0,3,128,0})==0xffffffff && p.last_error==32);++cases;
    CHECK(call("DeleteFileA",{primary})==0 && p.last_error==32);++cases;CHECK(call("CloseHandle",{handle})==1);++cases;
    CHECK(read(private_root/"Sys.sav")==std::vector<std::uint8_t>({'W','R','O','N','G'}) && !std::filesystem::exists(private_root/"Documents"));++cases;
    const auto save=p.put_string("Documents\\Leaf\\WHITE ALBUM2\\save_00.sav");handle=call("CreateFileA",{save,0xc0000000,7,0,2,128,0});CHECK(handle!=0xffffffff);++cases;
    p.memory.store(memory,0x12345678,32);CHECK(call("WriteFile",{handle,memory,4,memory+128,0})==1 && p.memory.load(memory+128,32)==4);++cases;
    CHECK(call("SetFilePointer",{handle,1,0,0})==1);p.memory.store(memory,0xee,8);CHECK(call("WriteFile",{handle,memory,1,memory+128,0})==1);++cases;
    CHECK(call("FlushFileBuffers",{handle})==1 && call("GetFileSize",{handle,0})==4);++cases;CHECK(call("CloseHandle",{handle})==1);++cases;
    CHECK(read(selected/"save_00.sav")==std::vector<std::uint8_t>({0x78,0xee,0x34,0x12}) && !std::filesystem::exists(private_root/"save_00.sav"));++cases;
    CHECK(call("CreateFileA",{p.put_string("SAVE_00.SAV"),0x40000000,7,0,1,128,0})==0xffffffff && p.last_error==80);++cases;
    CHECK(call("CreateFileA",{p.put_string("save_99.sav"),0x80000000,7,0,3,128,0})==0xffffffff && p.last_error==2);++cases;
    const auto pattern=p.put_string("Documents\\Leaf\\WHITE ALBUM2\\save_*.sav");auto find=call("FindFirstFileA",{pattern,memory});CHECK(find!=0xffffffff && p.read_string(memory+44)=="save_00.sav" && p.memory.load(memory+32,32)==4);++cases;CHECK(call("FindClose",{find})==1);++cases;
    const auto section=p.put_string("DEFAULT"),key=p.put_string("bgm_vol"),ini=p.put_string("Documents\\Leaf\\WHITE ALBUM2\\SYSTEM.ini");CHECK(call("GetPrivateProfileIntA",{section,key,99,ini})==37);++cases;
    CHECK(call("GetPrivateProfileIntA",{section,p.put_string("value"),99,p.put_string("default.ini")})==17);++cases;
    CHECK(call("WritePrivateProfileStringA",{section,key,p.put_string("42"),ini})==1);CHECK(c::Profile::read(selected/"SYSTEM.ini").get("DEFAULT","bgm_vol")=="42" && c::Profile::read(selected/"SYSTEM.ini").get("DEFAULT","unknown")=="keep");++cases;
    CHECK(!std::filesystem::exists(private_root/"SYSTEM.ini") && !std::filesystem::exists(private_root/"Documents"));++cases;
    const auto chapter=p.put_string("Documents\\Leaf\\WHITE ALBUM2 -closing chapter-");CHECK(call("GetFileAttributesA",{chapter})==0xffffffff);++cases;
    CHECK(call("CreateDirectoryA",{chapter,0})==1 && std::filesystem::is_directory(selected/"Documents/Leaf/WHITE ALBUM2 -closing chapter-"));++cases;
    CHECK(call("WritePrivateProfileStringA",{section,key,p.put_string("19"),p.put_string("Documents\\Leaf\\WHITE ALBUM2 -closing chapter-\\SYSTEM.ini")})==1 && call("GetPrivateProfileIntA",{section,key,99,ini})==42);++cases;
    auto reopened=c::save_files(c::local_files(selected),false);U32 error{};auto* stream=reopened->open("Sys.sav",true,false,3,error);CHECK(stream);std::array<char,7> original{};CHECK(std::fread(original.data(),1,7,stream)==7 && std::string_view(original.data(),7)=="PC-SAVE");std::fclose(stream);++cases;
    const auto copy=p.put_string("save_01.sav"),renamed=p.put_string("save_02.sav");CHECK(call("CopyFileA",{save,copy,1})==1 && read(selected/"save_01.sav")==read(selected/"save_00.sav"));++cases;
    CHECK(call("CopyFileA",{save,copy,1})==0 && p.last_error==80);++cases;CHECK(call("MoveFileA",{copy,renamed})==1 && std::filesystem::exists(selected/"save_02.sav") && !std::filesystem::exists(selected/"save_01.sav"));++cases;
    CHECK(call("CopyFileA",{p.put_string("save_99.sav"),p.put_string("save_03.sav"),1})==0 && p.last_error==2 && !std::filesystem::exists(selected/"save_03.sav"));++cases;
    CHECK(call("DeleteFileA",{renamed})==1 && !std::filesystem::exists(selected/"save_02.sav"));++cases;
    CHECK(call("CreateFileA",{p.put_string("grp.pak"),0x40000000,7,0,2,128,0})==0xffffffff && p.last_error==5);++cases;
    CHECK(call("CreateFileA",{p.put_string("unexpected.bin"),0x40000000,7,0,2,128,0})==0xffffffff && p.last_error==5);++cases;
    bool escaped=false;try{call("GetFileAttributesA",{p.put_string("..\\escape.sav")});}catch(const std::exception&){escaped=true;}CHECK(escaped);++cases;
    const auto full_pc=root/"FullPc";std::filesystem::create_directories(full_pc/"Documents/Leaf/WHITE ALBUM2");std::ofstream(full_pc/"Documents/Leaf/WHITE ALBUM2/Sys.sav")<<"NESTED";auto nested=c::save_files(c::local_files(full_pc),false);stream=nested->open("Sys.sav",true,false,3,error);CHECK(stream);std::array<char,6> legacy{};CHECK(std::fread(legacy.data(),1,6,stream)==6 && std::string_view(legacy.data(),6)=="NESTED");std::fclose(stream);++cases;
    stream=nested->open("Sys.sav",false,true,5,error);CHECK(stream);CHECK(std::fclose(stream)==0 && std::filesystem::file_size(full_pc/"Documents/Leaf/WHITE ALBUM2/Sys.sav")==0 && !std::filesystem::exists(full_pc/"Sys.sav"));++cases;
    const auto normal=read(selected/"Sys.sav");auto test=c::save_files(c::local_files(selected),true);CHECK(read(selected/"Sys.sav")==normal && test->info("Sys.sav")->size==wa2::testing::system_save_bytes);++cases;
    auto test_save=read(selected/"test-mode/Sys.sav");CHECK(!wa2::testing::unlock_all_content(test_save));++cases;
    CHECK(files->info("Documents/Leaf/WHITE ALBUM2/Sys.sav")->size==7 && !std::filesystem::exists(private_root/"test-mode"));++cases;
    const auto picked=root/"Picked";CHECK(std::filesystem::create_directory(picked));p.state().selected_paths["c:/picked"]={picked,true};const auto picked_name=p.put_string("C:\\Picked\\external.sav");handle=call("CreateFileA",{picked_name,0x40000000,7,0,2,128,0});CHECK(handle!=0xffffffff);++cases;
    CHECK(call("WriteFile",{handle,memory,4,memory+128,0})==1 && call("CloseHandle",{handle})==1 && std::filesystem::file_size(picked/"external.sav")==4);++cases;
    const auto unicode_root=root/std::filesystem::path(u8"\u6e38\u620f\u76ee\u5f55");CHECK(std::filesystem::create_directory(unicode_root));std::ofstream(unicode_root/"grp.pak",std::ios::binary)<<"OWN";auto unicode_mount=c::local_resource_mount(unicode_root);auto* unicode_file=unicode_mount->open("grp.pak");CHECK(unicode_file);CHECK(std::fgetc(unicode_file)=='O' && std::fclose(unicode_file)==0);++cases;
    CHECK(p.state().files.size()==base_handles);++cases;
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.selected-save-files.v1\",\"cases\":"<<cases<<",\"root_pc_files_read\":true,\"source_path_aliases\":true,\"alias_sharing_preserved\":true,\"random_access_writes\":true,\"profile_roundtrip\":true,\"nested_pc_fallback\":true,\"test_profile_isolated\":true,\"private_fallback_forbidden\":true,\"synthetic_files_only\":true,\"original_saves_opened\":false,\"devices_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";out.close();CHECK(out);std::cout<<"PASS selected-directory original-format save and configuration I/O.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
