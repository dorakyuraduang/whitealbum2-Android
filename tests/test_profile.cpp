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
    unsigned profiles{},flags_checked{},loaded_blocks{},source_reads{},guard_cases{},gallery_reads{},history_reads{};
    std::vector<std::uint8_t> expected_save(t::system_save_bytes);
    std::fill_n(expected_save.begin(),t::read_history_bytes,0xff);
    for(auto flag:t::content_flags)expected_save[t::system_flags_offset+flag*4]=1;
    for(unsigned bank=0;bank<2;++bank)for(auto variant:t::gallery_variants)
        expected_save[t::read_history_bytes+bank*t::gallery_bank_bytes+variant]=1;
    for(const auto* name:{"WHITE ALBUM2","WHITE ALBUM2 -closing chapter-","WHITE ALBUM2 -introductory chapter-"}){
        const auto data=bytes(test/"Documents"/"Leaf"/name/"Sys.sav");CHECK(data.size()==2528384 && data.size()==t::system_save_bytes);
        CHECK(data==expected_save);
        ++profiles;
    }
    // Production mutation must retain all unknown flags and every high bit.
    std::vector<std::uint8_t> varied(t::system_save_bytes);std::mt19937 random(0x405390);
    for(auto& value:varied)value=std::uint8_t(random());const auto before=varied;t::unlock_all_content(varied);
    for(unsigned n=0;n<varied.size();++n){CHECK(varied[n]==(before[n]|expected_save[n]));if(n>=t::system_flags_offset)++flags_checked;}
    CHECK(!t::unlock_all_content(varied));
    bool invalid=false;try{t::unlock_all_content(std::span(varied).first(varied.size()-1));}catch(const std::invalid_argument&){invalid=true;}CHECK(invalid);
    const auto system=test/"Documents"/"Leaf"/"WHITE ALBUM2"/"Sys.sav";
    const auto quick=system.parent_path()/"save_Q.sav";write(quick,normal_bytes);
    for(bool linear:{false,true}){
        wr::ProcessOptions options;options.data_root=test;options.allow_file_write=true;options.prefer_linear_memory=linear;
        wa2::aot::Runtime runtime(options);auto& process=runtime.process();runtime.load();
        CHECK(!process.gui() && !process.directdraw());
        const auto directory=process.put_string("C:\\WinRecomp\\Documents\\Leaf\\WHITE ALBUM2");
        process.callback(0x405110,std::array<U32,1>{directory},false);
        // Only existing save readers/writer and memory-only content getters run.
        // No game startup, menu, renderer, audio or resource payload runs.
        process.callback(0x405250,std::array<U32,1>{0},false);++loaded_blocks;
        process.callback(0x4052d0,std::array<U32,1>{0},false);++loaded_blocks;
        process.callback(0x405390,std::array<U32,1>{0},false);++loaded_blocks;
        for(auto flag:t::content_flags){CHECK(process.callback(0x401200,std::array<U32,1>{flag},false)==1);++source_reads;}
        CHECK(process.callback(0x401200,std::array<U32,1>{99},false)==0);
        CHECK(process.callback(0x401200,std::array<U32,1>{900},false)==0);
        std::vector<std::uint8_t> read(t::read_history_bytes),gallery(t::gallery_bytes);
        process.memory.copy_out(0x567360,read);process.memory.copy_out(0x84fb70,gallery);
        CHECK(std::all_of(read.begin(),read.end(),[](auto v){return v==0xff;}));
        CHECK(std::equal(gallery.begin(),gallery.end(),expected_save.begin()+t::read_history_bytes));
        for(U32 script:{0,1,511,1023})for(U32 line:{0,7,8,4095}){
            CHECK(process.callback(0x44e160,std::array<U32,4>{0x567360+script*512,1,line,0},false)==1);++history_reads;
        }
        // Independent catalogue check: read the actual initialized table used
        // by the static menu, then enumerate every group and valid difference.
        unsigned groups{},variants{};std::vector<unsigned> catalogue;
        for(U32 group=0;group<168;++group){
            std::vector<U32> entries,slots;
            for(U32 slot=0;slot<10;++slot){const auto id=process.memory.load(0x4c01b0+group*40+slot*4,32);if(id){entries.push_back(id);slots.push_back(slot);catalogue.push_back(id);}}
            groups+=!entries.empty();variants+=unsigned(entries.size());
            for(U32 bank=0;bank<2;++bank){
                CHECK(process.callback(0x428220,std::array<U32,2>{bank,group},false)==!entries.empty());++gallery_reads;
                if(entries.empty())continue;
                CHECK(process.callback(0x428300,std::array<U32,2>{bank,group},false)==entries.front());++gallery_reads;
                for(U32 nth=0;nth<entries.size();++nth){
                    CHECK(process.callback(0x428350,std::array<U32,3>{bank,group,nth},false)==slots[nth]);++gallery_reads;
                }
                CHECK(process.callback(0x428350,std::array<U32,3>{bank,group,U32(entries.size())},false)==0xffffffff);++gallery_reads;
            }
        }
        std::sort(catalogue.begin(),catalogue.end());catalogue.erase(std::unique(catalogue.begin(),catalogue.end()),catalogue.end());
        CHECK(groups==149 && catalogue.size()==272 && variants==272);
        CHECK(std::equal(catalogue.begin(),catalogue.end(),t::gallery_variants.begin()));
        CHECK(gallery[0]==0 && gallery[t::gallery_bank_bytes]==0);
        for(U32 track=0;track<64;++track){const auto id=process.memory.load(0x4c1dd0+track*4,32);
            if(id)CHECK(process.memory.load(0x521930+(100+id)*4,32)==1);
        }
        for(U32 scene=0;scene<24;++scene){const auto flag=process.memory.load(0x4c1bf0+scene*20+4,16);
            CHECK(process.memory.load(0x521930+flag*4,32)==1);
        }
        process.memory.store(0x521930+1019*4,0x918efac7,32);
        process.callback(0x405370,{},false);
        CHECK(process.executed_instructions==0);
        const auto saved=bytes(system);CHECK(saved[t::system_flags_offset+1019*4]==0xc7);
        p::select_test_profile(root,true);CHECK(bytes(system)==saved && bytes(quick)==normal_bytes);
        // Restore the unknown word for the next independent reader case.
        auto reset=saved;std::fill_n(reset.begin()+t::system_flags_offset+1019*4,4,0);write(system,reset);
    }
    // Upgrade an old chapter-only test profile without replacing manual saves.
    std::vector<std::uint8_t> old(t::system_save_bytes);
    for(auto flag:t::chapter_flags)old[t::system_flags_offset+flag*4]=1;
    old[t::system_flags_offset+99*4]=0x72;old[t::system_flags_offset+900*4]=0x35;
    old[t::read_history_bytes+4321]=0x79;write(system,old);
    p::select_test_profile(root,true);auto upgraded=expected_save;
    upgraded[t::system_flags_offset+99*4]=0x72;upgraded[t::system_flags_offset+900*4]=0x35;
    upgraded[t::read_history_bytes+4321]=0x79;CHECK(bytes(system)==upgraded && bytes(quick)==normal_bytes);
    auto changed=upgraded;changed[t::system_flags_offset+5*4]=0;changed[117]=0x79;
    changed[t::read_history_bytes+t::gallery_variants.back()]=0;write(system,changed);
    p::select_test_profile(root,true);CHECK(bytes(system)==upgraded);
    const auto unchanged_time=std::filesystem::last_write_time(system);p::select_test_profile(root,true);
    CHECK(bytes(system)==upgraded && std::filesystem::last_write_time(system)==unchanged_time);
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
    std::ofstream report(argv[1]);report<<"{\"schema\":\"wa2.content-test-profile.v2\",\"seeded_profiles\":"<<profiles<<",\"chapter_flags\":"<<t::chapter_flags.size()<<",\"content_flags\":"<<t::content_flags.size()<<",\"preserved_flag_bytes\":"<<flags_checked
        <<",\"actual_static_save_reader_blocks\":"<<loaded_blocks<<",\"actual_static_flag_reads\":"<<source_reads<<",\"invalid_private_save_rejections\":"<<guard_cases
        <<",\"gallery_groups\":149,\"gallery_variants\":272,\"actual_static_gallery_reads\":"<<gallery_reads<<",\"actual_static_history_reads\":"<<history_reads
        <<",\"music_flags\":64,\"movie_flags\":16,\"replay_flags\":24,\"old_profile_upgraded\":true,\"empty_gallery_slots_preserved\":true,\"unchanged_save_not_rewritten\":true"
        <<",\"save_bytes\":"<<t::system_save_bytes<<",\"normal_saves_unchanged\":true,\"test_manual_saves_retained\":true,\"unlock_reapplied_on_launch\":true,\"original_resource_files_opened\":false,\"source_executable_opened\":false,\"screen_access\":false,\"game_launched\":false,\"game_ui_exercised\":false,\"android_device_verified\":false,\"passed\":true}\n";
    CHECK(report.good());std::cout<<"PASS isolated test profile: content flags, static CG enumeration, reading history, migration, normal-save isolation and external mounts.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
