#include "progress_storage.hpp"
#include <algorithm>
#include <bit>
#include <fstream>
#include <iostream>
#include <map>
#include <random>

namespace p=wa2::progress;namespace platform=wa2::platform;namespace r=wa2::resource;
using Bytes=p::Bytes;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+ #x);}while(false)
template<class F>void rejects(F&& f){bool failed=false;try{f();}catch(const std::exception&){failed=true;}CHECK(failed);}
struct Source:r::Source {
    std::shared_ptr<Bytes> bytes;unsigned& live;std::size_t& reads;
    Source(std::shared_ptr<Bytes> data,unsigned& count,std::size_t& read):bytes(std::move(data)),live(count),reads(read){++live;}
    ~Source()override{--live;}
    std::uint64_t size()const override{return bytes->size();}
    void read_at(std::uint64_t at,std::span<std::uint8_t> out)override{CHECK(at<=bytes->size() && out.size()<=bytes->size()-at);std::copy_n(bytes->begin()+std::ptrdiff_t(at),out.size(),out.begin());reads+=out.size();}
};
struct Directory:platform::GameDirectory {
    struct Update {std::string name;std::uint64_t offset;std::size_t bytes;};
    std::map<std::string,std::shared_ptr<Bytes>> files;std::vector<Update> updates;unsigned live{},publications{},fail_update{};std::size_t reads{},written{};bool partial{};
    std::shared_ptr<r::Source> open(std::string_view name)override{CHECK(platform::game_file_path(name));const auto found=files.find(std::string(name));return found==files.end()?nullptr:std::make_shared<Source>(found->second,live,reads);}
    void replace(std::string_view name,std::span<const std::uint8_t> bytes)override{CHECK(platform::game_file_path(name,true) && !live);files[std::string(name)]=std::make_shared<Bytes>(bytes.begin(),bytes.end());++publications;written+=bytes.size();}
    void update(std::string_view name,std::uint64_t expected,std::uint64_t at,std::span<const std::uint8_t> bytes)override{
        CHECK(platform::game_file_path(name,true) && !live);auto& target=*files.at(std::string(name));CHECK(target.size()==expected && at<=expected && bytes.size()<=expected-at);
        updates.push_back({std::string(name),at,bytes.size()});if(fail_update && updates.size()==fail_update){if(partial && !bytes.empty()){target[std::size_t(at)]=bytes[0];++written;}throw r::Error("owned partial progress write failure");}
        std::copy(bytes.begin(),bytes.end(),target.begin()+std::ptrdiff_t(at));written+=bytes.size();
    }
    std::vector<platform::GameFileInfo> saves(std::string_view)override{return {};}
    void counters(){CHECK(!live);updates.clear();publications=0;reads=written=0;fail_update=0;partial=false;}
};
struct OldStorage:p::Storage {unsigned calls{};std::size_t bytes{};std::optional<Bytes> load()override{return {};}void write(p::Block kind,std::span<const std::uint8_t> value)override{CHECK(value.size()==p::size(kind));++calls;bytes+=value.size();}};
void range(const p::State& state,p::Block kind,std::size_t begin,std::size_t end){const auto value=state.dirty_range(kind);CHECK(value.begin==begin && value.end==end && state.dirty(kind)==(begin!=end));}
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{},guards{};std::mt19937 random(0x53595352);p::State blank;
    for(auto kind:{p::Block::history,p::Block::gallery,p::Block::flags})range(blank,kind,0,p::size(kind));++cases;
    Bytes original(p::layout::system_save_bytes);for(auto& byte:original)byte=std::uint8_t(random());original[p::layout::system_flags_offset+99*4]=1;original[p::layout::read_history_bytes-1]&=0xfe;
    auto directory=std::make_shared<Directory>();directory->files["Sys.sav"]=std::make_shared<Bytes>(original);p::Store store(platform::progress_storage(directory));
    for(auto kind:{p::Block::history,p::Block::gallery,p::Block::flags})range(store.state(),kind,0,0);CHECK(store.state().encode()==original && directory->reads==original.size());directory->counters();++cases;
    auto& state=store.state();const auto same=[&]{CHECK(*directory->files.at("Sys.sav")==state.encode() && directory->files.at("Sys.sav")->size()==original.size() && !directory->reads && !directory->publications);};
    state.set_gallery(0,100100,std::uint8_t(state.gallery(0,100100)^1));range(state,p::Block::gallery,100100,100101);store.flush();same();CHECK(directory->updates.size()==1 && directory->written==1 && directory->updates[0].offset==p::offset(p::Block::gallery)+100100 && directory->updates[0].bytes==1);++cases;
    directory->counters();state.set_gallery(0,100100,state.gallery(0,100100));store.flush();same();CHECK(directory->updates.empty() && !directory->written);++cases;
    // Start with known unread bits while preserving opaque bytes elsewhere.
    auto known=state.encode();known[0]=known[1]=0;directory->files["Sys.sav"]=std::make_shared<Bytes>(known);p::Store reading(platform::progress_storage(directory));directory->counters();reading.state().mark_read(0,2);reading.state().mark_read(0,6);range(reading.state(),p::Block::history,0,1);reading.flush();CHECK(directory->updates.size()==1 && directory->updates[0].offset==0 && directory->written==1 && directory->files.at("Sys.sav")->at(0)==0x22 && *directory->files.at("Sys.sav")==reading.state().encode());++cases;
    directory->files["Sys.sav"]=std::make_shared<Bytes>(state.encode());directory->counters();
    state.set_flag(310,std::bit_cast<std::int32_t>(std::uint32_t(state.flag(310))^0x10203040u));range(state,p::Block::flags,1240,1244);store.flush();same();CHECK(directory->updates.size()==1 && directory->updates[0].offset==p::layout::system_flags_offset+1240 && directory->written==4);++cases;
    directory->counters();state.set_flag(99,17);range(state,p::Block::flags,396,400);store.flush();same();CHECK(state.flag(99)==0 && directory->written==4);++cases;
    directory->counters();state.set_gallery(0,3,std::uint8_t(state.gallery(0,3)^1));state.set_gallery(1,7,std::uint8_t(state.gallery(1,7)^1));range(state,p::Block::gallery,3,p::layout::gallery_bank_bytes+8);store.flush();same();CHECK(directory->updates.size()==1 && directory->updates[0].offset==p::offset(p::Block::gallery)+3 && directory->written==p::layout::gallery_bank_bytes+5);++cases;
    directory->counters();state.set_flag(7,state.flag(7)^1);state.set_flag(11,state.flag(11)^1);range(state,p::Block::flags,28,48);store.flush();same();CHECK(directory->written==20);++cases;
    directory->counters();state.set_gallery(1,p::layout::gallery_bank_bytes-1,std::uint8_t(state.gallery(1,p::layout::gallery_bank_bytes-1)^1));range(state,p::Block::gallery,p::size(p::Block::gallery)-1,p::size(p::Block::gallery));store.flush();same();CHECK(directory->written==1 && directory->updates[0].offset==p::layout::system_flags_offset-1);++cases;
    directory->counters();state.set_flag(1023,state.flag(1023)^1);range(state,p::Block::flags,4092,4096);store.flush();same();CHECK(directory->written==4 && directory->updates[0].offset==p::layout::system_save_bytes-4);++cases;
    directory->counters();state.mark_read(p::layout::script_count-1,p::layout::lines_per_script-1);range(state,p::Block::history,p::layout::read_history_bytes-1,p::layout::read_history_bytes);store.flush();same();CHECK(directory->written==1 && directory->updates[0].offset==p::layout::read_history_bytes-1);++cases;
    for(bool partial:{false,true}){directory->counters();state.set_flag(70,std::bit_cast<std::int32_t>(std::uint32_t(state.flag(70))^0x80808080u));const auto before=*directory->files.at("Sys.sav");directory->fail_update=1;directory->partial=partial;rejects([&]{store.flush();});range(state,p::Block::flags,280,284);CHECK(directory->updates.size()==1 && directory->updates[0].bytes==4);if(!partial)CHECK(*directory->files.at("Sys.sav")==before);else CHECK(std::equal(before.begin(),before.begin()+std::ptrdiff_t(p::layout::system_flags_offset+280),directory->files.at("Sys.sav")->begin()) && std::equal(before.begin()+std::ptrdiff_t(p::layout::system_flags_offset+284),before.end(),directory->files.at("Sys.sav")->begin()+std::ptrdiff_t(p::layout::system_flags_offset+284)));++guards;
        directory->fail_update=0;store.flush();same();range(state,p::Block::flags,0,0);CHECK(directory->updates.size()==2 && directory->updates.back().bytes==4);++cases;
    }
    directory->counters();state.set_gallery(0,15,std::uint8_t(state.gallery(0,15)^1));state.set_flag(15,state.flag(15)^1);directory->fail_update=2;rejects([&]{store.flush();});range(state,p::Block::gallery,0,0);range(state,p::Block::flags,60,64);directory->fail_update=0;store.flush();same();CHECK(directory->updates.size()==3 && directory->updates[0].bytes==1 && directory->updates[2].bytes==4);++guards;++cases;
    for(int delta:{-1,1}){auto damaged=std::make_shared<Directory>();damaged->files["Sys.sav"]=std::make_shared<Bytes>(original);p::Store valid(platform::progress_storage(damaged));valid.state().set_flag(1,9);if(delta<0)damaged->files.at("Sys.sav")->pop_back();else damaged->files.at("Sys.sav")->push_back(0);const auto saved=*damaged->files.at("Sys.sav");rejects([&]{valid.flush();});CHECK(damaged->updates.empty() && *damaged->files.at("Sys.sav")==saved && valid.state().dirty(p::Block::flags));++guards;++cases;}
    {auto fresh=std::make_shared<Directory>();p::Store initial(platform::progress_storage(fresh));initial.state().set_flag(3,1);initial.flush();CHECK(fresh->publications==1 && fresh->files.at("Sys.sav")->size()==p::layout::system_save_bytes && *fresh->files.at("Sys.sav")==initial.state().encode());initial.flush();CHECK(fresh->updates.size()==2 && fresh->publications==1);}++cases;
    {OldStorage fallback;auto whole=blank.block(p::Block::flags);fallback.write_range(p::Block::flags,whole,{4,8});CHECK(fallback.calls==1 && fallback.bytes==p::layout::system_flags_bytes);++cases;
        for(auto invalid:std::array<p::DirtyRange,4>{{{0,0},{8,4},{0,whole.size()+1},{std::size_t(-1),std::size_t(-1)}}}){rejects([&]{fallback.write_range(p::Block::flags,whole,invalid);});++guards;}
        rejects([&]{fallback.write_range(p::Block::flags,whole.first(2),{0,1});});rejects([&]{fallback.write_range(p::Block(3),whole,{0,1});});guards+=2;CHECK(fallback.calls==1);
    }
    {auto isolated=std::make_shared<Directory>();isolated->files["Sys.sav"]=std::make_shared<Bytes>(original);isolated->files["native-test/Sys.sav"]=std::make_shared<Bytes>(original);p::Store test(platform::progress_storage(platform::game_test_directory(isolated)));isolated->counters();test.state().set_flag(3,test.state().flag(3)^1);test.flush();CHECK(isolated->updates.size()==1 && isolated->updates[0].name=="native-test/Sys.sav" && isolated->updates[0].bytes==4 && *isolated->files.at("Sys.sav")==original && *isolated->files.at("native-test/Sys.sav")==test.state().encode());}++cases;
    for(unsigned n=0;n<48;++n){directory->counters();const auto type=n%3;if(type==0){const auto bank=random()%2;const auto index=random()%p::layout::gallery_bank_bytes;state.set_gallery(bank,index,std::uint8_t(state.gallery(bank,index)^1));}else if(type==1){const auto index=random()%p::layout::flag_count;state.set_flag(index,state.flag(index)^1);}else state.mark_read(random()%p::layout::script_count,random()%p::layout::lines_per_script);
        store.flush();same();CHECK(directory->updates.size()<=1 && directory->written<=(type==1?4u:1u));for(auto kind:{p::Block::history,p::Block::gallery,p::Block::flags})range(state,kind,0,0);
    }++cases;
    {directory->counters();auto backend=platform::progress_storage(directory);const auto whole=state.block(p::Block::flags);rejects([&]{backend->write_range(p::Block::flags,whole,{1,1});});rejects([&]{backend->write_range(p::Block::flags,whole.first(2),{0,1});});rejects([&]{backend->write_range(p::Block(3),whole,{0,1});});rejects([&]{state.dirty_range(p::Block(3));});guards+=4;CHECK(directory->updates.empty() && !directory->publications);same();}
    {p::State unlocked;unlocked.unlock_all_for_testing();auto active=p::State::decode(unlocked.encode());active.set_flag(1023,active.flag(1023)^1);const auto value=active.flag(1023);active.unlock_all_for_testing();CHECK(active.flag(1023)==value);range(active,p::Block::flags,4092,4096);p::State intro;intro.set_gallery(0,100100,1);auto merged=p::State::decode(blank.encode());CHECK(merged.merge_introductory(intro));range(merged,p::Block::gallery,0,p::size(p::Block::gallery));}++cases;
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.native-progress-ranges.v1\",\"cases\":"<<cases<<",\"guards\":"<<guards<<",\"gallery_update_bytes\":1,\"reading_update_bytes\":1,\"flag_update_bytes\":4,\"range_union_checked\":true,\"failed_ranges_retryable\":true,\"unknown_bytes_preserved\":true,\"writes_read_no_existing_payload\":true,\"full_block_adapter_fallback\":true,\"missing_file_initialized_fully\":true,\"test_directory_isolated\":true,\"synthetic_memory_only\":true,\"original_saves_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";CHECK(out);std::cout<<"PASS changed Sys.sav ranges, bounded payloads, retained unknown bytes and failed-write retries.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
