#include "preference_storage.hpp"
#include "private_file.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>

namespace g=wa2::game;namespace fs=std::filesystem;namespace p=wa2::platform;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+ #x);}while(false)
template<class F>void rejects(F&& f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}CHECK(caught);}
using Bytes=std::vector<std::uint8_t>;
struct MemorySource:wa2::resource::Source {
    Bytes bytes;explicit MemorySource(std::string_view value):bytes(value.begin(),value.end()){}
    std::uint64_t size()const override{return bytes.size();}
    void read_at(std::uint64_t offset,std::span<std::uint8_t> out)override{CHECK(offset<=bytes.size() && out.size()<=bytes.size()-offset);std::copy_n(bytes.begin()+offset,out.size(),out.begin());}
};
void file(const fs::path& path,std::span<const std::uint8_t> bytes){std::ofstream out(path,std::ios::binary|std::ios::trunc);out.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));out.close();CHECK(out);}
Bytes file(const fs::path& path){std::ifstream in(path,std::ios::binary);CHECK(in);return Bytes(std::istreambuf_iterator<char>(in),{});}
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{},guards{};const auto root=fs::absolute(fs::path(argv[1])).parent_path()/("native-preferences-fixture-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));CHECK(fs::create_directory(root));const auto path=root/"native-settings.wa2cfg";const Bytes sentinel{3,1,4,1,5};file(root/"Sys.sav",sentinel);file(root/"SYSTEM.ini",sentinel);file(root/"save_000.sav",sentinel);
    auto storage=p::preference_storage(root);g::PreferenceStore prefs(storage);CHECK(prefs.current()==g::Preferences{} && !fs::exists(path));++cases;
    auto next=prefs.current();next.master=128;next.skin=2;next.captions=false;CHECK(prefs.replace(next) && g::Preferences::decode(file(path))==next);++cases;
    {g::PreferenceStore reopened(p::preference_storage(root));CHECK(reopened.current()==next);}++cases;
    const auto time=fs::last_write_time(path);CHECK(!prefs.replace(next) && fs::last_write_time(path)==time);++cases;
    auto invalid=next;invalid.auto_ticks=-1;rejects([&]{prefs.replace(invalid);});CHECK(file(path)==next.encode() && prefs.current()==next);++guards;
    next.text_step=0;next.skip_read_only=false;CHECK(prefs.replace(next) && file(path)==next.encode());for(const auto& entry:fs::directory_iterator(root))CHECK(entry.path().filename().string().find(".tmp-")==std::string::npos);++cases;
    CHECK(file(root/"Sys.sav")==sentinel && file(root/"SYSTEM.ini")==sentinel && file(root/"save_000.sav")==sentinel && !fs::exists(root/"native-saves"));++cases;
    auto corrupt=next.encode();corrupt.back()^=1;file(path,corrupt);rejects([&]{g::PreferenceStore damaged(storage);});CHECK(file(path)==corrupt);++guards;
    file(path,sentinel);rejects([&]{g::PreferenceStore too_short(storage);});++guards;
    storage->replace(next.encode());CHECK(g::PreferenceStore(storage).current()==next);++cases;
    rejects([&]{storage->replace(sentinel);});CHECK(file(path)==next.encode());++guards;
    const Bytes large(4097);rejects([&]{storage->replace(large);});CHECK(file(path)==next.encode());++guards;
    const auto linked_root=root/"linked";CHECK(fs::create_directory(linked_root));fs::create_hard_link(root/"Sys.sav",linked_root/"native-settings.wa2cfg");auto linked=p::preference_storage(linked_root);rejects([&]{linked->read();});rejects([&]{linked->replace(next.encode());});CHECK(file(root/"Sys.sav")==sentinel);guards+=2;
    const auto directory_root=root/"directory";CHECK(fs::create_directory(directory_root));CHECK(fs::create_directory(directory_root/"native-settings.wa2cfg"));auto directory=p::preference_storage(directory_root);rejects([&]{directory->read();});rejects([&]{directory->replace(next.encode());});guards+=2;
    auto absent=p::preference_storage(root/"absent");rejects([&]{absent->read();});rejects([&]{absent->replace(next.encode());});guards+=2;
    const auto unicode=root/fs::path(u8"\u8bbe\u7f6e");CHECK(fs::create_directory(unicode));auto utf=p::preference_storage(unicode);g::PreferenceStore unicode_prefs(utf);CHECK(unicode_prefs.replace(next) && g::PreferenceStore(utf).current()==next);++cases;
    rejects([&]{p::private_file::read(path,20,19);});rejects([&]{p::private_file::replace(path,next.encode(),200,100);});CHECK(file(path)==next.encode());guards+=2;
    const auto imported_root=root/"imported";CHECK(fs::create_directory(imported_root));std::vector<std::string> opened;
    const auto system=std::make_shared<MemorySource>("[DEFAULT]\nall_vol=72\nvoice=0\nmsg_wait=4"),display=std::make_shared<MemorySource>("[DEFAULT]\nwindow_mode=1\nWIN_SIZE_W=800\nWIN_SIZE_H=450");
    p::LegacySettingsOpener source=[&](auto name)->std::shared_ptr<wa2::resource::Source>{CHECK(p::legacy_settings_path(name));opened.emplace_back(name);if(name=="Documents/Leaf/WHITE ALBUM2/SYSTEM.ini")return system;if(name=="Documents/Leaf/WHITE ALBUM2/D3D.ini")return display;return {};};
    auto importing=p::preference_storage(imported_root,source);g::PreferenceStore first(importing);CHECK(first.current().master==72 && !first.current().voice_enabled && first.current().text_step==1 && first.current().window_size==2 && opened==std::vector<std::string>({"SYSTEM.ini","D3D.ini","Documents/Leaf/WHITE ALBUM2/SYSTEM.ini","Documents/Leaf/WHITE ALBUM2/D3D.ini"}));++cases;
    const auto imported_bytes=file(imported_root/"native-settings.wa2cfg");CHECK(g::Preferences::decode(imported_bytes)==first.current());source=[](auto)->std::shared_ptr<wa2::resource::Source>{throw std::runtime_error("Source import must never run twice");};CHECK(g::PreferenceStore(p::preference_storage(imported_root,source)).current()==first.current());++cases;
    const auto priority=root/"priority";CHECK(fs::create_directory(priority));opened.clear();g::PreferenceStore root_only(p::preference_storage(priority,[&](auto name)->std::shared_ptr<wa2::resource::Source>{opened.emplace_back(name);if(name=="SYSTEM.ini")return system;CHECK(name=="D3D.ini");return {};}));CHECK(root_only.current().master==72 && root_only.current().window_size==0 && opened==std::vector<std::string>({"SYSTEM.ini","D3D.ini"}));++cases;
    for(const auto kind:{"provider","oversized","malformed"}){const auto failed=root/kind;CHECK(fs::create_directory(failed));rejects([&]{g::PreferenceStore bad(p::preference_storage(failed,[&](auto)->std::shared_ptr<wa2::resource::Source>{if(std::string_view(kind)=="provider")throw wa2::resource::Error("injected original settings provider failure");return std::make_shared<MemorySource>(std::string_view(kind)=="oversized"?std::string(65537,';'):"[DEFAULT]\nall_vol=bad");}));});CHECK(fs::is_empty(failed));++guards;++cases;}
    CHECK(file(root/"SYSTEM.ini")==sentinel && file(root/"Sys.sav")==sentinel);++cases;
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.native-preference-storage.v1\",\"cases\":"<<cases<<",\"guards\":"<<guards<<",\"synthetic_files_only\":true,\"original_settings_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";CHECK(out);std::cout<<"PASS native atomic preferences and unchanged-write suppression using synthetic files.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
