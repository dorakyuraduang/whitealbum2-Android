#include "../platform/compat/resource_mount.hpp"
#include "../platform/compat/system.hpp"
#include "../platform/compat/profile.hpp"
#include "../recompiled/movie_settings.hpp"
#include <winrecomp/process.hpp>
#include <winrecomp/win32_state.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
using namespace wr;
Image fixture_image(){Image image;image.base=0x400000;image.entry=0x401000;image.size=0x10000;image.headers_size=4096;image.bytes.resize(8192);image.sections={{".text",4096,4096,4096,4096,0x60000020}};return image;}
int main(int argc,char** argv){try{
    CHECK(argc==2);const auto root=std::filesystem::path(argv[1]).parent_path()/("mount-fixture-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    CHECK(std::filesystem::create_directory(root));const auto source_file=root/"synthetic-source.bin",private_root=root/"private";CHECK(std::filesystem::create_directory(private_root));
    {std::ofstream file(source_file,std::ios::binary);file.write("PAK0test",8);CHECK(file.good());}
    ProcessOptions options;options.data_root=private_root;options.allow_file_write=true;
    Process p(FunctionExecutor{[](Cpu&,Memory&){return false;}},options);wa2::platform::compat::install_system(p);unsigned opens{};
    const auto mount=std::make_shared<wa2::platform::compat::ResourceMount>(std::vector<ReadOnlyFileInfo>{{"BGM.PAK",8,1234567},{"IC/Data.pak",8,7654321}},[&](std::string_view name){
        CHECK(name=="BGM.PAK" || name=="IC/Data.pak");++opens;return std::fopen(source_file.string().c_str(),"rb");});
    wa2::platform::compat::install_resource_mount(p,mount);p.load(fixture_image());const auto work=p.allocate_bytes(4096);
    auto call=[&](const char* name,std::initializer_list<U32> args){const auto target=p.resolve("kernel32.dll",name);CHECK(target);return p.callback(target,{args.begin(),args.size()});};
    const auto bgm=p.put_string("bgm.pak"),intro=p.put_string("ic\\data.PAK"),ic=p.put_string("ic"),root_name=p.put_string("C:\\WinRecomp");
    CHECK(call("GetFileAttributesA",{bgm})==0x21 && call("GetFileAttributesA",{ic})==0x11 && opens==0);
    const auto a=call("CreateFileA",{bgm,0x80000000,1,0,3,0x80,0}),b=call("CreateFileA",{bgm,0x80000000,1,0,3,0x80,0});CHECK(a!=0xffffffff && b!=0xffffffff && a!=b && opens==2);
    CHECK(call("GetFileSize",{a,work+32})==8 && p.memory.load(work+32,32)==0);
    CHECK(call("ReadFile",{a,work,4,work+32,0})==1 && p.memory.load(work,32)==0x304b4150 && p.memory.load(work+32,32)==4);
    auto direct=wa2::platform::compat::open_process_resource(p,guest_file_path(p,"bgm.pak"));CHECK(opens==3 && direct->size()==8);
    std::array<std::uint8_t,4> read{};direct->read_at(4,read);CHECK((read==std::array<std::uint8_t,4>{'t','e','s','t'}));direct->read_at(0,read);CHECK(opens==3);
    CHECK(call("ReadFile",{a,work,4,work+32,0})==1 && p.memory.load(work,32)==0x74736574);
    CHECK(call("ReadFile",{b,work,4,work+32,0})==1 && p.memory.load(work,32)==0x304b4150);
    CHECK(call("CreateFileA",{bgm,0x40000000,1,0,2,0x80,0})==0xffffffff && p.last_error==5);
    CHECK(call("DeleteFileA",{bgm})==0 && p.last_error==5);CHECK(!std::filesystem::exists(private_root/"bgm.pak"));
    const auto pattern=p.put_string("*.pak");const auto find=call("FindFirstFileA",{pattern,work});CHECK(find!=0xffffffff && p.read_string(work+44)=="BGM.PAK" && p.memory.load(work+32,32)==8);
    CHECK(call("FindNextFileA",{find,work})==0 && p.last_error==18);CHECK(call("FindClose",{find})==1);
    CHECK(call("SetCurrentDirectoryA",{ic})==1 && p.state().current_directory=="IC");const auto relative=p.put_string("DATA.PAK");const auto c=call("CreateFileA",{relative,0x80000000,1,0,3,0x80,0});CHECK(c!=0xffffffff && opens==4);
    const auto child_find=call("FindFirstFileA",{pattern,work});CHECK(child_find!=0xffffffff && p.read_string(work+44)=="Data.pak" && p.memory.load(work+20,32)==7654321);CHECK(call("FindClose",{child_find})==1);
    CHECK(call("SetCurrentDirectoryA",{root_name})==1 && p.state().current_directory.empty());for(auto handle:{a,b,c})CHECK(call("CloseHandle",{handle})==1);
    CHECK(!std::filesystem::exists(private_root/"BGM.PAK") && !std::filesystem::exists(private_root/"IC"/"Data.pak"));
    unsigned failure_contexts{};
    // A private file is not a substitute for a missing external archive.
    // The error must name the actual logical request and indexed directory.
    {std::ofstream file(private_root/"missing.pak",std::ios::binary);file<<"wrong namespace";}
    for(const auto* name:{"missing.pak","IC/MV010.PAK"}){
        bool failed=false;
        try{wa2::platform::compat::open_process_resource(p,guest_file_path(p,name));}
        catch(const wa2::resource::NotFound& error){const std::string message=error.what();failed=message.find(name)!=message.npos && message.find("BGM.PAK")!=message.npos && message.find("game current directory")!=message.npos;}
        CHECK(failed);++failure_contexts;
    }
    {std::ofstream file(private_root/"settings.dat",std::ios::binary);file<<"save";}
    auto save=wa2::platform::compat::open_process_resource(p,guest_file_path(p,"settings.dat"));CHECK(save->size()==4);
    bool named_file_error=false;
    try{wa2::platform::compat::open_process_resource(p,guest_file_path(p,"missing.dat"));}
    catch(const wa2::resource::Error& error){named_file_error=std::string(error.what()).find("missing.dat")!=std::string::npos;}
    CHECK(named_file_error);++failure_contexts;
    unsigned movie_settings_cases{};
    const auto movie_setting=[&](std::vector<ReadOnlyFileInfo> entries,std::optional<U32> previous,std::optional<U32> expected,bool changed,bool writable=true){
        const auto folder=root/("movie-settings-"+std::to_string(movie_settings_cases));CHECK(std::filesystem::create_directory(folder));
        auto profile=wa2::platform::compat::Profile::read(folder/"default.ini");profile.set("DEFAULT","bgm_vol","37");
        if(previous)profile.set("DEFAULT","mov_lv",std::to_string(*previous));profile.save(folder/"default.ini");
        ProcessOptions opts;opts.data_root=folder;opts.allow_file_write=writable;
        Process process(FunctionExecutor{[](Cpu&,Memory&){throw std::runtime_error("movie settings must not enter game code");return false;}},opts);
        wa2::platform::compat::install_resource_mount(process,std::make_shared<wa2::platform::compat::ResourceMount>(std::move(entries),[](std::string_view)->FILE*{throw std::runtime_error("movie selection must not read PAK payloads");}));
        const auto selected=wa2::aot::configure_movie_resources(process,folder/"default.ini");CHECK(selected.selected==expected && selected.changed==changed);
        const auto after=wa2::platform::compat::Profile::read(folder/"default.ini");CHECK(after.get("DEFAULT","bgm_vol")==std::optional<std::string>("37"));
        const auto stored=after.get("DEFAULT","mov_lv");const auto value=changed?expected:previous;
        CHECK(stored==(value?std::optional(std::to_string(*value)):std::nullopt));
        if(expected){const auto again=wa2::aot::configure_movie_resources(process,folder/"default.ini");CHECK(again.selected==expected && !again.changed);}
        ++movie_settings_cases;
    };
    movie_setting({{"mv000.pak",16,0}}, {},2,true);
    movie_setting({{"mv000.pak",16,0}},1,2,true);
    movie_setting({{"mv000.pak",16,0},{"mv001.pak",16,0}},1,1,false);
    movie_setting({{"mv000.pak",16,0},{"mv001.pak",16,0}}, {},2,true);
    movie_setting({{"mv001.pak",16,0}},2,1,true);
    movie_setting({{"mv002.pak",16,0}}, {},0,true);
    movie_setting({{"MV000.PAK",16,0},{"mv100.pak",16,0},{"IC/MV010.PAK",16,0}},1,2,true);
    movie_setting({{"mv000.pak",16,0},{"IC/mv011.pak",16,0}},1,{},false);
    movie_setting({{"mv000.pak",16,0}},1,{},false,false);
    movie_setting({{"grp.pak",16,0}}, {},{},false);
    // The directory-backed Windows path makes the same choice without a mount.
    {const auto folder=root/"local-movie-settings";CHECK(std::filesystem::create_directory(folder));std::ofstream(folder/"MV000.PAK")<<"synthetic";
        ProcessOptions opts;opts.data_root=folder;opts.allow_file_write=true;Process local(FunctionExecutor{[](Cpu&,Memory&){return false;}},opts);
        const auto result=wa2::aot::configure_movie_resources(local,folder/"default.ini");CHECK(result.selected==2 && result.changed);++movie_settings_cases;}
    unsigned profile_boundary_cases{};
    for(const auto* chapter:{"WHITE ALBUM2","WHITE ALBUM2 -closing chapter-"}){
        const auto folder=root/("movie-profile-boundary-"+std::to_string(profile_boundary_cases));CHECK(std::filesystem::create_directory(folder));
        const auto settings=folder/"Documents"/"Leaf"/chapter/"SYSTEM.ini";CHECK(std::filesystem::create_directories(settings.parent_path()));
        ProcessOptions opts;opts.data_root=folder;opts.allow_file_write=true;Process process(FunctionExecutor{[](Cpu&,Memory&){return false;}},opts);
        wa2::platform::compat::install_system(process,wa2::aot::prepare_movie_profile);
        wa2::platform::compat::install_resource_mount(process,std::make_shared<wa2::platform::compat::ResourceMount>(std::vector<ReadOnlyFileInfo>{{"MV000.PAK",16,0},{"IC/MV010.PAK",16,0}},[](std::string_view)->FILE*{throw std::runtime_error("profile boundary must not open PAK payloads");}));
        process.load(fixture_image());const auto section=process.put_string("DeFaUlT"),key=process.put_string("MoV_Lv");
        const auto profile=process.put_string(std::string("C:\\WinRecomp\\Documents\\Leaf\\")+chapter+"\\SYSTEM.ini");
        // Unrelated reads must not create a profile or change defaults.
        CHECK(process.callback(process.resolve("kernel32.dll","GetPrivateProfileIntA"),std::array<U32,4>{section,process.put_string("bgm_vol"),37,profile})==37);
        CHECK(!std::filesystem::exists(settings));
        CHECK(process.callback(process.resolve("kernel32.dll","GetPrivateProfileIntA"),std::array<U32,4>{section,key,1,profile})==2);
        CHECK(wa2::platform::compat::Profile::read(settings).get("DEFAULT","mov_lv")==std::optional<std::string>("2"));
        CHECK(!std::filesystem::exists(folder/"default.ini"));const auto diagnostic=process.state().reports.at("movie_resource_settings")();
        CHECK(diagnostic.find("SYSTEM.ini")!=std::string::npos && diagnostic.find("\"changed\":true")!=std::string::npos);++profile_boundary_cases;
    }
    bool rejected=false;try{auto invalid=std::make_shared<wa2::platform::compat::ResourceMount>(std::vector<ReadOnlyFileInfo>{{"../bad.pak",0,0}},[](std::string_view){return static_cast<FILE*>(nullptr);});}catch(const std::invalid_argument&){rejected=true;}CHECK(rejected);
    std::ofstream report(argv[1]);report<<"{\"schema\":\"wa2.external-resource-mount.v1\",\"open_descriptor_calls\":"<<opens<<",\"failure_context_cases\":"<<failure_contexts<<",\"movie_resource_setting_cases\":"<<movie_settings_cases<<",\"actual_system_profile_boundary_cases\":"<<profile_boundary_cases<<",\"root_default_ini_not_created\":true,\"movie_selection_metadata_only\":true,\"existing_available_movie_setting_preserved\":true,\"mounted_pak_namespace_is_authoritative\":true,\"independent_stream_positions\":true,\"native_source_reads_without_reopening\":true,\"ic_and_case_insensitive_paths\":true,\"source_find_data_layout\":true,\"resource_writes_denied\":true,\"pak_payload_copied_to_private_storage\":false,\"original_resource_files_opened\":false,\"source_executable_opened\":false,\"screen_access\":false,\"game_launched\":false,\"passed\":true}\n";
    std::cout<<"PASS external resource mount: original file streams, native archive reads and IC paths without copying.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
