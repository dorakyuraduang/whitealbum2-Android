#include "../platform/compat/test_profile.hpp"
#include "../platform/compat/resource_mount.hpp"
#include "../engine/test_progress.hpp"
#include "process.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <random>

#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
namespace t=wa2::testing;
namespace p=wa2::platform::compat;
using wr::U32;
std::vector<std::uint8_t> bytes(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);CHECK(file.good());return {std::istreambuf_iterator<char>(file),{}};}
void write(const std::filesystem::path& path,std::span<const std::uint8_t> data){std::ofstream file(path,std::ios::binary|std::ios::trunc);file.write(reinterpret_cast<const char*>(data.data()),data.size());CHECK(file.good());}
int main(int argc,char** argv){try{
    CHECK(argc==2);const auto root=std::filesystem::absolute(std::filesystem::path(argv[1])).parent_path()/("test-profile-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    CHECK(std::filesystem::create_directory(root));
    CHECK(p::select_test_profile(root/"not-created",false)==root/"not-created" && !std::filesystem::exists(root/"not-created"));
    const auto normal=root/"Documents"/"Leaf"/"WHITE ALBUM2";CHECK(std::filesystem::create_directories(normal));
    const std::vector<std::uint8_t> normal_bytes={0x11,0x62,0xa8,0,0x99};
    write(normal/"Sys.sav",normal_bytes);write(normal/"save_Q.sav",normal_bytes);write(normal/"SYSTEM.ini",normal_bytes);
    const auto test=p::select_test_profile(root,true);CHECK(test==root/"test-mode");
    unsigned profiles{},flags_checked{},loaded_blocks{},source_reads{},guard_cases{};
    for(const auto* name:{"WHITE ALBUM2","WHITE ALBUM2 -closing chapter-","WHITE ALBUM2 -introductory chapter-"}){
        const auto data=bytes(test/"Documents"/"Leaf"/name/"Sys.sav");CHECK(data.size()==2528384 && data.size()==t::system_save_bytes);
        for(std::size_t n=0;n<data.size();++n){bool expected=false;for(auto flag:t::chapter_flags)expected|=n==t::system_flags_offset+flag*4;CHECK(data[n]==(expected?1:0));}
        ++profiles;
    }
    // Production mutation must retain all unknown flags and every high bit.
    std::array<std::uint8_t,t::system_flags_bytes> varied;std::mt19937 random(0x405390);
    for(auto& value:varied)value=std::uint8_t(random());const auto before=varied;t::unlock_chapters(varied);
    for(unsigned n=0;n<varied.size();++n){auto expected=before[n];for(auto flag:t::chapter_flags)if(n==flag*4)expected|=1;CHECK(varied[n]==expected);++flags_checked;}
    CHECK(!t::unlock_chapters(varied));
    const auto system=test/"Documents"/"Leaf"/"WHITE ALBUM2"/"Sys.sav";
    const auto quick=system.parent_path()/"save_Q.sav";write(quick,normal_bytes);
    for(bool linear:{false,true}){
        wr::ProcessOptions options;options.data_root=test;options.allow_file_write=true;options.prefer_linear_memory=linear;
        wa2::aot::Runtime runtime(options);auto& process=runtime.process();runtime.load();
        CHECK(!process.gui() && !process.directdraw());
        const auto directory=process.put_string("C:\\WinRecomp\\Documents\\Leaf\\WHITE ALBUM2");
        process.callback(0x405110,std::array<U32,1>{directory},false);
        // Only the three existing save readers and the flag getter are called.
        // No game startup, menu, renderer, audio or resource payload runs.
        process.callback(0x405250,std::array<U32,1>{0},false);++loaded_blocks;
        process.callback(0x4052d0,std::array<U32,1>{0},false);++loaded_blocks;
        process.callback(0x405390,std::array<U32,1>{0},false);++loaded_blocks;
        for(auto flag:t::chapter_flags){CHECK(process.callback(0x401200,std::array<U32,1>{flag},false)==1);++source_reads;}
        CHECK(process.callback(0x401200,std::array<U32,1>{99},false)==0);
        std::vector<std::uint8_t> read(t::read_history_bytes),gallery(t::gallery_bytes);
        process.memory.copy_out(0x567360,read);process.memory.copy_out(0x84fb70,gallery);
        CHECK(std::all_of(read.begin(),read.end(),[](auto v){return !v;}) && std::all_of(gallery.begin(),gallery.end(),[](auto v){return !v;}));
        process.memory.store(0x521930+1019*4,0x918efac7,32);
        process.callback(0x405370,{},false);
        CHECK(process.executed_instructions==0);
        const auto saved=bytes(system);CHECK(saved[t::system_flags_offset+1019*4]==0xc7);
        p::select_test_profile(root,true);CHECK(bytes(system)==saved && bytes(quick)==normal_bytes);
        // Restore the non-chapter word for the next independent reader case.
        auto reset=saved;std::fill_n(reset.begin()+t::system_flags_offset+1019*4,4,0);write(system,reset);
    }
    auto changed=bytes(system);changed[t::system_flags_offset+5*4]=0;changed[117]=0x79;write(system,changed);
    p::select_test_profile(root,true);changed[t::system_flags_offset+5*4]=1;CHECK(bytes(system)==changed);
    CHECK(p::select_test_profile(root,false)==root && bytes(normal/"Sys.sav")==normal_bytes && bytes(normal/"save_Q.sav")==normal_bytes && bytes(normal/"SYSTEM.ini")==normal_bytes);
    // Incorrect private formats and links fail instead of modifying user data.
    write(system,normal_bytes);bool rejected=false;try{p::select_test_profile(root,true);}catch(const std::runtime_error&){rejected=true;}CHECK(rejected && bytes(normal/"Sys.sav")==normal_bytes);++guard_cases;
    CHECK(std::filesystem::remove(system));std::filesystem::create_hard_link(normal/"Sys.sav",system);
    rejected=false;try{p::select_test_profile(root,true);}catch(const std::runtime_error&){rejected=true;}CHECK(rejected && bytes(normal/"Sys.sav")==normal_bytes);++guard_cases;
    CHECK(std::filesystem::remove(system));p::select_test_profile(root,true);
    // External resources remain available through the unchanged mount adapter.
    const auto archives=root/"external";CHECK(std::filesystem::create_directory(archives));CHECK(std::filesystem::create_directory(archives/"IC"));
    write(archives/"GRP.PAK",normal_bytes);write(archives/"IC"/"bak.pak",normal_bytes);write(archives/"not-a-pak.txt",normal_bytes);
    auto mount=p::local_resource_mount(archives);CHECK(mount->info("grp.pak")->size==normal_bytes.size() && mount->info("ic/BAK.PAK")->size==normal_bytes.size() && !mount->info("not-a-pak.txt"));
    auto* stream=mount->open("IC/BAK.PAK");CHECK(stream);std::array<std::uint8_t,5> from_stream{};CHECK(std::fread(from_stream.data(),1,from_stream.size(),stream)==from_stream.size());CHECK(std::fclose(stream)==0 && std::equal(from_stream.begin(),from_stream.end(),normal_bytes.begin()));
    CHECK(!std::filesystem::exists(test/"GRP.PAK") && !std::filesystem::exists(test/"IC"/"bak.pak"));
    std::ofstream report(argv[1]);report<<"{\"schema\":\"wa2.chapter-test-profile.v1\",\"seeded_profiles\":"<<profiles<<",\"chapter_flags\":"<<t::chapter_flags.size()<<",\"preserved_flag_bytes\":"<<flags_checked
        <<",\"actual_static_save_reader_blocks\":"<<loaded_blocks<<",\"actual_static_flag_reads\":"<<source_reads<<",\"invalid_private_save_rejections\":"<<guard_cases
        <<",\"save_bytes\":"<<t::system_save_bytes<<",\"normal_saves_unchanged\":true,\"test_manual_saves_retained\":true,\"unlock_reapplied_on_launch\":true,\"original_resource_files_opened\":false,\"source_executable_opened\":false,\"screen_access\":false,\"game_launched\":false,\"game_ui_exercised\":false,\"android_device_verified\":false,\"passed\":true}\n";
    CHECK(report.good());std::cout<<"PASS isolated test profile: source save readers, chapter flags, retained test saves, normal-save isolation and external mounts.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
