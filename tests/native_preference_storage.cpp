#include "preference_storage.hpp"
#include "private_file.hpp"
#include <chrono>
#include <fstream>
#include <iostream>

namespace g=wa2::game;namespace fs=std::filesystem;namespace p=wa2::platform;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+ #x);}while(false)
template<class F>void rejects(F&& f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}CHECK(caught);}
using Bytes=std::vector<std::uint8_t>;
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
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.native-preference-storage.v1\",\"cases\":"<<cases<<",\"guards\":"<<guards<<",\"synthetic_files_only\":true,\"original_settings_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";CHECK(out);std::cout<<"PASS native atomic preferences and unchanged-write suppression using synthetic files.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
