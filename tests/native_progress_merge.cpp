#include "progress.hpp"
#include "progress_storage.hpp"
#include "legacy_save_path.hpp"
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
void word(Bytes& bytes,std::size_t at,unsigned value){for(unsigned n=0;n<4;++n)bytes.at(at+n)=std::uint8_t(value>>(n*8));}
struct Source:r::Source {Bytes bytes;bool* fail;std::uint64_t size()const override{return bytes.size();}void read_at(std::uint64_t at,std::span<std::uint8_t> out)override{if(*fail)throw r::Error("injected introductory progress read failure");CHECK(at<=bytes.size() && out.size()<=bytes.size()-at);std::copy_n(bytes.begin()+std::ptrdiff_t(at),out.size(),out.begin());}};
struct Directory:platform::GameDirectory {
    std::map<std::string,Bytes> files;std::vector<std::string> opens;unsigned writes{};bool fail_read{},fail_intro_read{},fail_write{};
    std::shared_ptr<r::Source> open(std::string_view name)override{opens.emplace_back(name);const auto found=files.find(std::string(name));if(found==files.end())return {};auto source=std::make_shared<Source>();source->bytes=found->second;source->fail=name.starts_with(r::legacy_save_directories[3])?&fail_intro_read:&fail_read;return source;}
    void replace(std::string_view name,std::span<const std::uint8_t> bytes)override{CHECK(name=="Sys.sav" || name=="native-test/Sys.sav");if(fail_write)throw r::Error("injected progress merge publication failure");files[std::string(name)]=Bytes(bytes.begin(),bytes.end());++writes;}
    void update(std::string_view,std::uint64_t,std::uint64_t,std::span<const std::uint8_t>)override{throw r::Error("Startup merge must publish one complete file");}
    std::vector<platform::GameFileInfo> saves(std::string_view)override{return {};}
};
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{},guards{};
    p::State empty;const auto clean_bytes=empty.encode();auto clean=p::State::decode(clean_bytes);CHECK(!clean.merge_introductory(clean));for(auto kind:{p::Block::history,p::Block::gallery,p::Block::flags})CHECK(!clean.dirty(kind));++cases;
    std::mt19937 random(0x49434d47);for(unsigned sample=0;sample<4;++sample){Bytes first(p::layout::system_save_bytes),second(first.size());for(unsigned at=0;at<first.size();++at){first[at]=std::uint8_t(random());second[at]=std::uint8_t(random());}auto expected=first;
        for(unsigned at=0;at<expected.size();++at)if(at<p::layout::system_flags_offset+99*4 || at>=p::layout::system_flags_offset+100*4)expected[at]|=second[at];auto main=p::State::decode(first);const auto introductory=p::State::decode(second);CHECK(main.merge_introductory(introductory) && main.encode()==expected && introductory.encode()==second);for(auto kind:{p::Block::history,p::Block::gallery,p::Block::flags})CHECK(main.dirty(kind));auto repeated=p::State::decode(expected);CHECK(!repeated.merge_introductory(introductory) && repeated.encode()==expected);for(auto kind:{p::Block::history,p::Block::gallery,p::Block::flags})CHECK(!repeated.dirty(kind));++cases;
    }
    for(auto kind:{p::Block::history,p::Block::gallery,p::Block::flags}){auto main=p::State::decode(clean_bytes);p::State source;if(kind==p::Block::history)source.mark_read(19,37);else if(kind==p::Block::gallery)source.set_gallery(1,100100,0x80);else source.set_flag(900,std::bit_cast<int>(0x80000001u));CHECK(main.merge_introductory(source));for(auto block:{p::Block::history,p::Block::gallery,p::Block::flags})CHECK(main.dirty(block)==(block==kind));++cases;}
    auto raw=clean_bytes;word(raw,p::layout::system_flags_offset+99*4,0xffffffffu);auto preserved=p::State::decode(clean_bytes);CHECK(!preserved.merge_introductory(p::State::decode(raw)) && preserved.encode()==clean_bytes && !preserved.dirty(p::Block::flags));++cases;
    word(raw,p::layout::system_flags_offset+99*4,0x12345678);auto with_99=p::State::decode(raw);CHECK(!with_99.merge_introductory(p::State::decode(raw)) && with_99.encode()==raw);++cases;
    const auto intro_name=std::string(r::legacy_save_directories[3])+"Sys.sav";p::State main_state,intro_state;main_state.mark_read(0,0);main_state.set_flag(310,2);intro_state.mark_read(0,1);intro_state.set_gallery(0,100100,1);intro_state.set_flag(0,1);intro_state.set_flag(310,1);auto main_bytes=main_state.encode(),intro_bytes=intro_state.encode();word(main_bytes,p::layout::system_flags_offset+99*4,0x12345678);word(intro_bytes,p::layout::system_flags_offset+99*4,0xffffffffu);
    auto expected_state=p::State::decode(main_bytes);CHECK(expected_state.merge_introductory(p::State::decode(intro_bytes)));const auto expected=expected_state.encode();
    for(bool combined:{false,true})for(unsigned mode=0;mode<3;++mode){auto directory=std::make_shared<Directory>();const auto primary=std::string(r::legacy_save_directories[combined?1:2])+"Sys.sav";if(mode==0)directory->files["Sys.sav"]=main_bytes;else if(mode==1)directory->files[primary]=main_bytes;directory->files[intro_name]=intro_bytes;
        p::Store loaded(platform::progress_storage(directory,combined));const auto wanted=mode<2?expected:[&]{auto fresh=p::State::decode(clean_bytes);fresh.merge_introductory(p::State::decode(intro_bytes));return fresh.encode();}();CHECK(loaded.state().encode()==wanted && directory->files.at("Sys.sav")==wanted && directory->files.at(intro_name)==intro_bytes && directory->writes==1);if(mode==1)CHECK(directory->files.at(primary)==main_bytes);CHECK(loaded.state().flag(0)==1 && loaded.state().gallery(0,100100)==1 && loaded.state().read(0,1));if(mode<2)CHECK(loaded.state().flag(310)==3 && loaded.state().flag(99)==0x12345678);else CHECK(loaded.state().flag(99)==0);++cases;
        loaded.flush();CHECK(directory->writes==1);p::Store repeated(platform::progress_storage(directory,combined));CHECK(repeated.state().encode()==wanted && directory->writes==1);++cases;
        // Read errors and wrong lengths never mutate an already published root.
        directory->fail_read=true;const auto before=directory->files;rejects([&]{p::Store failed(platform::progress_storage(directory,combined));});CHECK(directory->files==before);directory->fail_read=false;++guards;
        directory->fail_intro_read=true;rejects([&]{p::Store failed(platform::progress_storage(directory,combined));});CHECK(directory->files==before && directory->opens.back()==intro_name);directory->fail_intro_read=false;++guards;
    }
    for(unsigned failure=0;failure<4;++failure){auto directory=std::make_shared<Directory>();directory->files["Sys.sav"]=main_bytes;directory->files[intro_name]=intro_bytes;if(failure==0)directory->files[intro_name].pop_back();else if(failure==1)directory->files[intro_name].push_back(0);else if(failure==2)directory->fail_write=true;else directory->files["Sys.sav"].pop_back();const auto before=directory->files;rejects([&]{p::Store failed(platform::progress_storage(directory,true));});CHECK(directory->files==before && !directory->writes);if(failure==3)CHECK(directory->opens==std::vector<std::string>{"Sys.sav"});++guards;++cases;}
    {auto directory=std::make_shared<Directory>();directory->files["Sys.sav"]=main_bytes;directory->files[intro_name]=intro_bytes;auto test=platform::game_test_directory(directory);p::Store isolated(platform::progress_storage(test,true));CHECK(isolated.state().encode()==clean_bytes && directory->files.size()==2 && directory->files.at("Sys.sav")==main_bytes && !directory->writes && directory->opens==std::vector<std::string>{"native-test/Sys.sav"});}++cases;
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.native-progress-merge.v1\",\"cases\":"<<cases<<",\"guards\":"<<guards<<",\"source_block_or_policy\":true,\"source_flag_99_excluded\":true,\"destination_flag_99_preserved\":true,\"unchanged_blocks_stay_clean\":true,\"read_only_source_retained\":true,\"repeat_startup_writes_nothing\":true,\"failed_merge_preserves_root\":true,\"test_profile_isolated\":true,\"synthetic_files_only\":true,\"original_saves_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";CHECK(out);std::cout<<"PASS original introductory progress merge, source masks and selected-directory publication.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
