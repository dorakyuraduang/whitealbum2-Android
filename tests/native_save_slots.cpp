#include "save_slots.hpp"
#include "save_storage.hpp"
#include "checkpoint_codec.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>

namespace g=wa2::game;namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
template<class F>void rejects(F&& f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}CHECK(caught);}
using Bytes=std::vector<std::uint8_t>;
struct Memory:g::SaveStorage {
    std::map<unsigned,Bytes> slots;bool failing{},invalid{},duplicate{};unsigned writes{};
    std::optional<Bytes> read(unsigned slot)override{auto found=slots.find(slot);return found==slots.end()?std::optional<Bytes>{}:found->second;}
    void replace(unsigned slot,std::span<const std::uint8_t> bytes)override{if(failing)throw std::runtime_error("injected native slot error");slots[slot]=Bytes(bytes.begin(),bytes.end());++writes;}
    std::vector<g::SaveSlotInfo> list()override{if(invalid)return {{1000,32,0}};std::vector<g::SaveSlotInfo> result;for(auto found=slots.rbegin();found!=slots.rend();++found)result.push_back({found->first,found->second.size(),1});if(duplicate && !result.empty())result.push_back(result.front());return result;}
};
g::SavedScene fixture(std::string name="synthetic"){g::SavedScene value;value.scene=std::move(name);for(unsigned n=0;n<value.parts.size();++n){wa2::checkpoint::Writer out;out.u32(n);value.parts[n]=out.seal(g::scene_part_tags[n]);}return value;}
void file(const fs::path& path,std::span<const std::uint8_t> bytes){std::ofstream out(path,std::ios::binary|std::ios::trunc);out.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));out.close();CHECK(out);}
Bytes file(const fs::path& path){std::ifstream in(path,std::ios::binary);CHECK(in);return Bytes(std::istreambuf_iterator<char>(in),{});}
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{},guards{};const auto first=fixture().encode(),second=fixture("another_scene").encode();auto memory=std::make_shared<Memory>();g::SaveSlots slots(memory);CHECK(!slots.read(0) && slots.list().empty());++cases;
    slots.write(8,first);slots.write(2,second);const auto list=slots.list();CHECK(list.size()==2 && list[0].slot==2 && list[1].slot==8 && *slots.read(8)==first);++cases;
    slots.write(8,second);CHECK(*slots.read(8)==second && memory->writes==3);++cases;
    auto corrupt=first;corrupt.back()^=1;rejects([&]{slots.write(8,corrupt);});CHECK(*slots.read(8)==second && memory->writes==3);++guards;
    memory->failing=true;rejects([&]{slots.write(8,first);});CHECK(*slots.read(8)==second);memory->failing=false;++guards;
    for(unsigned slot:{1000u,unsigned(-1)}){rejects([&]{slots.read(slot);});rejects([&]{slots.write(slot,first);});guards+=2;}
    rejects([&]{g::SaveSlots bad({});});++guards;memory->invalid=true;rejects([&]{slots.list();});memory->invalid=false;++guards;memory->duplicate=true;rejects([&]{slots.list();});memory->duplicate=false;++guards;
    memory->slots[7]=corrupt;rejects([&]{slots.read(7);});++guards;
    const auto root=fs::absolute(fs::path(argv[1])).parent_path()/("native-slots-fixture-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));CHECK(fs::create_directory(root));const Bytes sentinel{3,1,4,1,5};file(root/"Sys.sav",sentinel);file(root/"SYSTEM.ini",sentinel);file(root/"save_Q.sav",first);
    auto storage=wa2::platform::save_storage(root);g::SaveSlots disk(storage);CHECK(!disk.read(0) && disk.list().empty() && !fs::exists(root/"native-saves"));++cases;
    disk.write(3,first);disk.write(0,second);disk.write(999,first);CHECK(*disk.read(3)==first && *disk.read(0)==second && *disk.read(999)==first);++cases;
    const auto files=disk.list();CHECK(files.size()==3 && files[0].slot==0 && files[1].slot==3 && files[2].slot==999 && files[0].modified_ms>0 && files[2].bytes==first.size());++cases;
    disk.write(3,second);CHECK(*disk.read(3)==second);for(const auto& item:fs::directory_iterator(root/"native-saves"))CHECK(item.path().filename().string().find(".tmp-")==std::string::npos);++cases;
    const auto modified=fs::last_write_time(root/"native-saves/slot-003.wa2sav");rejects([&]{disk.write(3,corrupt);});CHECK(*disk.read(3)==second && fs::last_write_time(root/"native-saves/slot-003.wa2sav")==modified);++guards;
    rejects([&]{storage->replace(1000,first);});rejects([&]{storage->read(1000);});guards+=2;
    CHECK(file(root/"Sys.sav")==sentinel && file(root/"SYSTEM.ini")==sentinel && file(root/"save_Q.sav")==first);++cases;
    CHECK(fs::create_directory(root/"native-saves/slot-007.wa2sav"));rejects([&]{disk.write(7,first);});rejects([&]{disk.read(7);});guards+=2;
    fs::create_hard_link(root/"save_Q.sav",root/"native-saves/slot-008.wa2sav");rejects([&]{disk.read(8);});rejects([&]{disk.write(8,second);});CHECK(file(root/"save_Q.sav")==first);guards+=2;
    file(root/"native-saves/slot-009.wa2sav",sentinel);rejects([&]{disk.read(9);});++guards;const auto damaged=disk.list();CHECK(damaged.size()==6);unsigned damaged_count{};for(const auto& item:damaged)damaged_count+=!item.diagnostic.empty();CHECK(damaged_count==3);++cases;
    disk.write(9,first);CHECK(*disk.read(9)==first);++cases;
    const auto absent=wa2::platform::save_storage(root/"absent");rejects([&]{absent->replace(0,first);});++guards;
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.native-save-slots.v1\",\"cases\":"<<cases<<",\"guards\":"<<guards<<",\"slot_capacity\":1000,\"synthetic_files_only\":true,\"original_saves_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";CHECK(out);std::cout<<"PASS native atomic save slots and original-save isolation using synthetic files.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
