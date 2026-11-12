// Own files and unchanged static resource helpers only. No game/UI execution.
#include "process.hpp"
#include "../platform/compat/resource_mount.hpp"
#include "../platform/compat/save_files.hpp"
#include "../platform/compat/language_resources.hpp"
#include <winrecomp/win32_state.hpp>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <thread>
using wr::U32;namespace c=wa2::platform::compat;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("loose resource check at ")+std::to_string(__LINE__)+": "+#x);}while(false)
using Bytes=std::vector<std::uint8_t>;
void word(Bytes& bytes,std::size_t at,U32 value){for(unsigned n=0;n<4;++n)bytes[at+n]=std::uint8_t(value>>(n*8));}
Bytes pak(const std::vector<std::string>& names,std::uint8_t fill){Bytes result(16+names.size()*44);std::memcpy(result.data(),"KCAP",4);word(result,12,U32(names.size()));
    for(std::size_t n=0;n<names.size();++n){CHECK(names[n].size()<=24);const auto at=16+n*44;std::memcpy(result.data()+at+4,names[n].data(),names[n].size());word(result,at+36,U32(result.size()));word(result,at+40,3);result.insert(result.end(),{fill,std::uint8_t(n),0x77});}return result;}
void put(const std::filesystem::path& path,const Bytes& bytes){std::filesystem::create_directories(path.parent_path());std::ofstream output(path,std::ios::binary|std::ios::trunc);output.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));output.close();CHECK(output);}
Bytes read(std::shared_ptr<wr::ReadOnlyFiles> mount,std::string_view name){auto info=mount->info(name);CHECK(info && !(info->attributes&16));std::unique_ptr<std::FILE,decltype(&std::fclose)> stream(mount->open(name),std::fclose);CHECK(stream);Bytes result(std::size_t(info->size));CHECK(std::fread(result.data(),1,result.size(),stream.get())==result.size());return result;}
struct Result {unsigned reads{},guards{},groups{},encodings{};};
Result exercise(const std::filesystem::path& root,bool linear){
    Result result;const auto selected=root/(linear?"linear-selected":"paged-selected"),private_root=root/(linear?"linear-private":"paged-private");std::filesystem::create_directory(selected);std::filesystem::create_directory(private_root);
    for(const auto* group:{"grp","bak","char","fon","ck-gal"}){put(selected/(std::string(group)+".pak"),pak({"sample.bin","priority.bin","fallback.bin"},0x21));put(selected/group/"sample.bin",{0xa1,0xa2,0xa3,0xa4});}
    put(selected/"patch.pak",pak({"priority.bin"},0x31));put(selected/"grp"/"priority.bin",{0xb1,0xb2});
    put(selected/"IC"/"grp.pak",pak({"sample.bin"},0x41));put(selected/"IC"/"grp"/"sample.bin",{0xc1,0xc2});
    put(private_root/"grp"/"fallback.bin",{0xdd});put(private_root/"grp"/"missing.bin",{0xee});
    put(selected/"grp"/"nested"/"sub.bin",{0x55});
    const auto unicode=selected/"fon"/std::filesystem::u8path(u8"本体80.tga");put(unicode,{0x61,0x62});
    put(selected/"grp"/std::filesystem::u8path(u8"中文.bin"),{0x63});
    put(selected/"grp"/std::filesystem::u8path(u8"丄.bin"),{0x64});put(selected/"grp"/std::filesystem::u8path(u8"表.bin"),{0x65});
    wr::ProcessOptions options;options.data_root=private_root;options.allow_file_write=true;options.prefer_linear_memory=linear;
    wa2::aot::Runtime runtime(options);auto& p=runtime.process();p.state().mutable_files=c::save_files(c::local_files(selected),false);
    auto mount=c::local_resource_mount(selected);c::install_resource_mount(p,mount);runtime.load();CHECK(!p.gui() && !p.directdraw());const auto output=p.allocate_bytes(4);
    const auto api=[&](const char* name,std::initializer_list<U32> args){return p.callback(p.resolve("kernel32.dll",name),std::span(args.begin(),args.size()));};
    const auto load=[&](const char* group,const char* name,const Bytes& expected){p.memory.store(output,0,32);const auto size=p.callback(0x437820,std::array<U32,3>{group?p.put_string(group):0,p.put_string(name),output},false);
        CHECK(size==expected.size());const auto pointer=p.memory.load(output,32);CHECK(pointer);Bytes actual(expected.size());p.memory.copy_out(pointer,actual);CHECK(actual==expected);CHECK(api("HeapFree",{api("GetProcessHeap",{}),0,pointer})==1);++result.reads;};
    for(const auto* group:{"grp","bak","char","fon","ck-gal"}){load(group,"sample.bin",{0xa1,0xa2,0xa3,0xa4});++result.groups;}
    load("grp","priority.bin",{0xb1,0xb2});++result.guards;
    std::filesystem::remove(selected/"grp"/"priority.bin");load("grp","priority.bin",{0x31,0,0x77});++result.guards;
    load("grp","fallback.bin",{0x21,2,0x77});++result.guards;
    load("IC\\grp","sample.bin",{0xc1,0xc2});load(nullptr,"grp\\nested\\sub.bin",{0x55});++result.guards;
    load("GRP","SAMPLE.BIN",{0xa1,0xa2,0xa3,0xa4});++result.guards;
    put(selected/"grp"/"sample.bin",{0x91,0x92,0x93,0x94,0x95});load("grp","sample.bin",{0x91,0x92,0x93,0x94,0x95});++result.guards;
    // Metadata/name snapshots expire; file contents and handles are never cached.
    CHECK(!mount->info("grp/new.bin"));put(selected/"grp"/"new.bin",{0x88});std::this_thread::sleep_for(std::chrono::milliseconds(270));load("grp","new.bin",{0x88});++result.guards;
    CHECK(p.callback(0x4376c0,std::array<U32,2>{p.put_string("grp"),p.put_string("new.bin")},false)!=0);CHECK(p.callback(0x4376c0,std::array<U32,2>{p.put_string("grp"),p.put_string("missing.bin")},false)==0);++result.guards;
    CHECK(read(mount,"fon/\x96\x7b\x91\xcc" "80.tga")==Bytes({0x61,0x62}));++result.encodings;
    const auto u8=std::filesystem::path(std::filesystem::u8path(u8"fon/本体80.tga")).generic_u8string();CHECK(read(mount,std::string(u8.begin(),u8.end()))==Bytes({0x61,0x62}));++result.encodings;
    CHECK(read(mount,"grp/\xd6\xd0\xce\xc4.bin")==Bytes({0x63}));++result.encodings;
    CHECK(read(mount,"grp/\x81\x41.bin")==Bytes({0x64}));++result.encodings;
    load("grp","\x95\x5c.bin",{0x65});++result.guards;
    CHECK(mount->info("grp") && (mount->info("grp")->attributes&16));CHECK(!mount->list("grp").empty());++result.guards;
    for(const auto* path:{"grp/../Sys.sav","../grp/sample.bin","/grp/sample.bin","grp/a:b","grp/a?b","grp//sample.bin","grp/a/../../outside","Documents/Leaf/WHITE ALBUM2/Sys.sav"}){CHECK(!mount->owns_path(path) && !mount->info(path));++result.guards;}
    CHECK(api("CreateFileA",{p.put_string("grp/missing.bin"),0x80000000,7,0,3,128,0})==0xffffffff && p.last_error==2);++result.guards;
    const auto source=p.put_string("grp/sample.bin"),missing=p.put_string("grp/buf/sample.bin");
    CHECK(api("CopyFileA",{source,missing,1})==0 && p.last_error==5);CHECK(!std::filesystem::exists(selected/"grp"/"buf"));++result.guards;
    CHECK(api("CreateFileA",{missing,0x40000000,7,0,2,128,0})==0xffffffff && p.last_error==5);++result.guards;
    CHECK(api("DeleteFileA",{source})==0 && p.last_error==5);CHECK(api("DeleteFileA",{missing})==0 && p.last_error==5);++result.guards;
    CHECK(api("MoveFileA",{source,missing})==0 && p.last_error==5);CHECK(api("SetFileAttributesA",{source,128})==0 && p.last_error==5);++result.guards;
    CHECK(api("CreateDirectoryA",{p.put_string("grp/buf"),0})==0 && p.last_error==5);++result.guards;
    // The unrelated writable namespace remains usable at the canonical path.
    const auto save=api("CreateFileA",{p.put_string("save_00.sav"),0x40000000,7,0,2,128,0});CHECK(save!=0xffffffff);CHECK(api("CloseHandle",{save})==1);CHECK(std::filesystem::exists(selected/"Documents/Leaf/WHITE ALBUM2/save_00.sav"));++result.guards;
    CHECK(!p.gui() && !p.directdraw());return result;
}
int main(int argc,char** argv){try{
    CHECK(argc==2);const auto root=std::filesystem::path(argv[1]).parent_path()/("loose-resources-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));CHECK(std::filesystem::create_directory(root));Result sum;
    for(bool linear:{false,true}){const auto r=exercise(root,linear);sum.reads+=r.reads;sum.guards+=r.guards;sum.groups+=r.groups;sum.encodings+=r.encodings;}
    std::ofstream output(argv[1]);output<<"{\"schema\":\"wa2.loose-resources.v1\",\"static_resource_reader_calls\":"<<sum.reads<<",\"static_resource_check_calls\":4,\"guard_cases\":"<<sum.guards<<",\"resource_group_cases\":"<<sum.groups<<",\"encoded_filename_cases\":"<<sum.encodings<<",\"external_before_patch_and_pak\":true,\"missing_external_falls_back\":true,\"ic_and_case_insensitive_paths\":true,\"fresh_stream_contents\":true,\"name_cache_expiry\":true,\"resource_namespace_read_only\":true,\"source_optional_backup_not_fatal\":true,\"private_resource_fallback_forbidden\":true,\"canonical_saves_preserved\":true,\"original_resources_opened\":false,\"source_executable_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";CHECK(output);
    std::cout<<"PASS unchanged static resource reader: loose groups, PAK fallback, readonly namespace, fresh streams and encoding lookup.\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
