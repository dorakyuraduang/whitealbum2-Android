#include "game_directory.hpp"
#include "progress_storage.hpp"
#include "preference_storage.hpp"
#include "save_storage.hpp"
#include "legacy_save_path.hpp"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>

namespace p=wa2::platform;namespace g=wa2::game;namespace progress=wa2::progress;namespace r=wa2::resource;
using Bytes=std::vector<std::uint8_t>;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+ #x);}while(false)
template<class F>void rejects(F&& f){bool failed=false;try{f();}catch(const std::exception&){failed=true;}CHECK(failed);}
struct Source:r::Source {
    Bytes bytes;std::size_t* read{};bool* fail{};explicit Source(Bytes b):bytes(std::move(b)){}
    std::uint64_t size()const override{return bytes.size();}
    void read_at(std::uint64_t offset,std::span<std::uint8_t> out)override{if(fail && *fail)throw r::Error("injected provider read failure");CHECK(offset<=bytes.size() && out.size()<=bytes.size()-offset);if(read)*read+=out.size();std::copy_n(bytes.begin()+offset,out.size(),out.begin());}
};
struct Directory:p::GameDirectory {
    std::map<std::string,Bytes> files;std::vector<std::string> opened;std::vector<p::GameFileInfo> inventory;std::size_t read_bytes{},written_bytes{};unsigned publications{},updates{};bool fail_open{},fail_read{},fail_write{};
    std::shared_ptr<r::Source> open(std::string_view name)override{CHECK(p::game_file_path(name));opened.emplace_back(name);if(fail_open)throw r::Error("injected provider open failure");const auto found=files.find(std::string(name));if(found==files.end())return {};auto source=std::make_shared<Source>(found->second);source->read=&read_bytes;source->fail=&fail_read;return source;}
    void replace(std::string_view name,std::span<const std::uint8_t> bytes)override{CHECK(p::game_file_path(name,true));if(fail_write)throw r::Error("injected publication failure");files[std::string(name)]=Bytes(bytes.begin(),bytes.end());++publications;written_bytes+=bytes.size();}
    void update(std::string_view name,std::uint64_t expected,std::uint64_t offset,std::span<const std::uint8_t> bytes)override{CHECK(p::game_file_path(name,true));if(fail_write)throw r::Error("injected range-write failure");auto& target=files.at(std::string(name));CHECK(target.size()==expected && offset<=expected && bytes.size()<=expected-offset);std::copy(bytes.begin(),bytes.end(),target.begin()+offset);++updates;written_bytes+=bytes.size();}
    std::vector<p::GameFileInfo> saves(std::string_view name)override{CHECK(name=="native-saves" || name=="native-test/native-saves");return inventory;}
};
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{},guards{};
    for(unsigned slot=0;slot<1000;++slot){char name[32];std::snprintf(name,sizeof(name),"slot-%03u.wa2sav",slot);CHECK(p::game_slot_file(name)==slot && p::game_file_path(std::string("native-saves/")+name,true));++cases;}
    for(const auto name:{"Sys.sav","native-settings.wa2cfg","native-saves/slot-999.wa2sav"}){CHECK(p::game_file_path(name) && p::game_file_path(name,true));++cases;}
    for(const auto prefix:r::legacy_save_directories){const auto name=std::string(prefix)+"Sys.sav";CHECK(p::game_file_path(name) && (p::game_file_path(name,true)==prefix.empty()));++cases;}
    for(const auto name:{"","..","../Sys.sav","native-saves/../Sys.sav","/Sys.sav","C:/Sys.sav","native-saves\\slot-000.wa2sav","native-saves/slot-1000.wa2sav","native-saves/slot-abc.wa2sav","native-saves/slot-000.wa2sav.tmp","grp.pak","SYSTEM.ini","save_00.sav","native-settings.wa2cfg/evil"}){CHECK(!p::game_file_path(name) && !p::game_file_path(name,true));++guards;}
    progress::State original;original.set_flag(5,1);original.set_flag(310,0x4567);original.set_gallery(1,210301,129);original.mark_read(120,42);auto original_bytes=original.encode();original_bytes[0x81234]=0xab;
    for(bool combined:{false,true}){
        auto directory=std::make_shared<Directory>();const auto primary=std::string(r::legacy_save_directories[combined?1:2])+"Sys.sav";directory->files[primary]=original_bytes;
        progress::Store imported(p::progress_storage(directory,combined));CHECK(imported.state().encode()==original_bytes && directory->files.at("Sys.sav")==original_bytes && directory->files.at(primary)==original_bytes && directory->publications==1 && directory->opened==std::vector<std::string>({"Sys.sav",primary}));++cases;
        imported.flush();CHECK(directory->updates==0 && directory->publications==1);++cases;
        imported.state().set_flag(310,9);imported.flush();CHECK(directory->updates==1 && directory->written_bytes==original_bytes.size()+4096 && directory->files.at(primary)==original_bytes && progress::State::decode(directory->files.at("Sys.sav")).flag(310)==9 && directory->files.at("Sys.sav")[0x81234]==0xab);++cases;
        directory->opened.clear();progress::Store reopened(p::progress_storage(directory,combined));CHECK(reopened.state().flag(310)==9 && directory->opened==std::vector<std::string>{"Sys.sav"} && directory->publications==1);++cases;
        directory->fail_write=true;reopened.state().set_flag(311,12);rejects([&]{reopened.flush();});CHECK(reopened.state().dirty(progress::Block::flags) && progress::State::decode(directory->files.at("Sys.sav")).flag(311)==0);directory->fail_write=false;reopened.flush();CHECK(progress::State::decode(directory->files.at("Sys.sav")).flag(311)==12);++guards;++cases;
        for(unsigned length:{0u,1u,2528383u,2528385u}){directory->files["Sys.sav"]=Bytes(length);const auto before=directory->files;rejects([&]{progress::Store bad(p::progress_storage(directory,combined));});CHECK(directory->files==before && directory->opened.back()=="Sys.sav");++guards;}
        for(unsigned failure=0;failure<4;++failure){auto failed=std::make_shared<Directory>();failed->files[primary]=original_bytes;failed->fail_open=failure==0;failed->fail_read=failure==1;failed->fail_write=failure==2;if(failure==3)failed->files[primary].pop_back();const auto before=failed->files;rejects([&]{progress::Store bad(p::progress_storage(failed,combined));});CHECK(failed->files==before && !failed->files.contains("Sys.sav"));++guards;}
    }
    auto first=std::make_shared<Directory>(),second=std::make_shared<Directory>();g::PreferenceStore settings(p::preference_storage(first));auto preferences=settings.current();preferences.master=51;settings.replace(preferences);CHECK(first->files.contains("native-settings.wa2cfg") && second->files.empty());++cases;
    g::PreferenceStore other(p::preference_storage(second));CHECK(other.current()==g::Preferences{});other.replace(preferences);CHECK(g::PreferenceStore(p::preference_storage(second)).current()==preferences && g::PreferenceStore(p::preference_storage(first)).current()==preferences);++cases;
    auto saves=p::save_storage(first);Bytes snapshot(200000,0x51);saves->replace(42,snapshot);first->read_bytes=0;CHECK(saves->prefix(42,512)==std::optional<Bytes>(Bytes(512,0x51)) && first->read_bytes==512 && first->files.at("native-saves/slot-042.wa2sav")==snapshot && !second->files.contains("native-saves/slot-042.wa2sav"));++cases;
    first->inventory={{"slot-042.wa2sav",snapshot.size(),200,{}}};CHECK(saves->list().size()==1 && saves->list()[0].slot==42 && saves->list()[0].modified_ms==200);++cases;
    first->inventory.push_back(first->inventory.front());rejects([&]{saves->list();});first->inventory={{"bad.pak",0,0,{}}};rejects([&]{saves->list();});first->inventory={{"slot-007.wa2sav",2,0,{}}};CHECK(!saves->list()[0].diagnostic.empty());guards+=2;++cases;
    const auto before=first->files;for(const auto name:{"../Sys.sav","grp.pak","Documents/other/Sys.sav"}){rejects([&]{p::game_file_read(*first,name,0,100);});++guards;}rejects([&]{p::game_file_read(*first,"Sys.sav",10,9);});rejects([&]{p::game_file_read(*first,"Sys.sav",0,100,0);});guards+=2;CHECK(first->files==before);
    rejects([&]{p::progress_storage(std::shared_ptr<p::GameDirectory>{});});rejects([&]{p::preference_storage(std::shared_ptr<p::GameDirectory>{});});rejects([&]{p::save_storage(std::shared_ptr<p::GameDirectory>{});});guards+=3;
    auto test=p::game_test_directory(first);progress::Store test_progress(p::progress_storage(test));test_progress.state().unlock_all_for_testing();test_progress.flush();CHECK(first->files.contains("native-test/Sys.sav") && !first->files.contains("Sys.sav") && progress::State::decode(first->files.at("native-test/Sys.sav")).flag(5)==1);++cases;
    g::PreferenceStore test_settings(p::preference_storage(test));CHECK(test_settings.current()==g::Preferences{});test_settings.replace(preferences);CHECK(first->files.contains("native-test/native-settings.wa2cfg") && first->files.at("native-settings.wa2cfg")==preferences.encode());++cases;
    p::save_storage(test)->replace(12,snapshot);CHECK(first->files.contains("native-test/native-saves/slot-012.wa2sav") && !first->files.contains("native-saves/slot-012.wa2sav"));++cases;
    for(const auto name:{"native-test/native-test/Sys.sav","native-test/Documents/Leaf/WHITE ALBUM2/Sys.sav","native-test/../Sys.sav"}){CHECK(!p::game_file_path(name));++guards;}
    std::ofstream report(argv[1]);report<<"{\"schema\":\"wa2.native-game-directory.v1\",\"cases\":"<<cases<<",\"guards\":"<<guards<<",\"selected_directory_isolation\":true,\"root_sys_precedence\":true,\"read_only_original_import\":true,\"bounded_previews\":true,\"range_writes_preserve_other_blocks\":true,\"failed_writes_retryable\":true,\"synthetic_files_only\":true,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";CHECK(report);std::cout<<"PASS selected game-directory saves, configuration, original progress and bounded I/O.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
