// Finite source flag-setter calls on synthetic Sys.sav files. No title/game
// update, audio device, original resource, window or screen is exercised.
#include "process.hpp"
#include "music_flag_save.hpp"
#include "../platform/compat/save_files.hpp"
#include "../engine/test_progress.hpp"
#include <winrecomp/win32_state.hpp>
#include <chrono>
#include <fstream>
#include <iostream>

#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("line ")+std::to_string(__LINE__)+": "+#x);}while(false)
namespace a=wa2::aot;namespace c=wa2::platform::compat;using wr::U32;
class Counted final:public wr::MutableFiles {
    std::shared_ptr<wr::MutableFiles> files_;
public:
    unsigned opens{},queries{};bool fail{};
    explicit Counted(std::shared_ptr<wr::MutableFiles> value):files_(std::move(value)){}
    std::optional<wr::ReadOnlyFileInfo> info(std::string_view name)override{++queries;return files_->info(name);}
    std::vector<wr::ReadOnlyFileInfo> list(std::string_view name)override{return files_->list(name);}
    std::FILE* open(std::string_view name,bool read,bool write,U32 mode,U32& error)override{++opens;if(fail){error=5;return nullptr;}return files_->open(name,read,write,mode,error);}
    U32 directory(std::string_view name)override{return files_->directory(name);}
    U32 remove(std::string_view name,bool folder)override{return files_->remove(name,folder);}
    U32 move(std::string_view from,std::string_view to)override{return files_->move(from,to);}
    U32 attributes(std::string_view name,U32 flags)override{return files_->attributes(name,flags);}
    void replace(std::string_view name,std::span<const std::uint8_t> data)override{files_->replace(name,data);}
};
std::optional<a::MusicFlagStamp> stamp(void* owner){
    const auto& state=static_cast<wr::Process*>(owner)->state();if(state.current_thread_id!=1)return {};
    for(const auto& [id,file]:state.files)if(file.owned && file.stream && file.write)return {};
    const auto revision=c::save_files_revision(state.mutable_files.get());if(!revision)return {};
    return a::MusicFlagStamp{state.mutable_files.get(),*revision};
}
std::vector<std::uint8_t> read(const std::filesystem::path& path){std::ifstream input(path,std::ios::binary);CHECK(input);return {std::istreambuf_iterator<char>(input),{}};}
struct Totals {unsigned fixtures{},calls{},baseline_opens{},optimized_opens{},notifications{},guards{};};
void fixture(const std::filesystem::path& root,bool linear,bool unlocked,bool optimized,Totals& totals){
    CHECK(std::filesystem::create_directory(root));wr::ProcessOptions options;options.data_root=root;options.allow_file_write=true;options.prefer_linear_memory=linear;
    a::Runtime runtime(options);auto& p=runtime.process();auto& m=p.memory;
    auto backend=std::make_shared<Counted>(c::local_files(root));p.state().mutable_files=c::save_files(backend,false);
    unsigned notifications{};bool short_write{},bad_close{};
    p.register_api("fixture","flagChanged",2,[&](wr::Process& q){CHECK(q.argument(1)==1);++notifications;return 0u;},true);
    p.replace_api("kernel32.dll","GetProcAddress",2,[&](wr::Process&){return p.resolve("fixture","flagChanged");});
    // A failed close must invalidate acknowledgement even if WriteFile already
    // reported 4096. The production close also propagates stdio flush errors.
    p.replace_api("kernel32.dll","CloseHandle",1,[&](wr::Process& q){auto it=q.state().files.find(q.argument(0));CHECK(it!=q.state().files.end() && it->second.owned);const auto result=std::fclose(it->second.stream);q.state().files.erase(it);return result || bad_close?0u:1u;});
    p.replace_api("kernel32.dll","WriteFile",5,[&](wr::Process& q){auto& file=q.state().files.at(q.argument(0));CHECK(file.owned && file.write && q.argument(2)==4096);std::array<std::uint8_t,4096> bytes;m.copy_out(q.argument(1),bytes);
        const auto count=std::fwrite(bytes.data(),1,short_write?2048:4096,file.stream);m.store(q.argument(3),U32(count),32);return count==4096?1u:0u;});
    runtime.load();CHECK(!p.gui() && !p.directdraw());
    std::vector<std::uint8_t> save(wa2::testing::system_save_bytes);if(unlocked)wa2::testing::unlock_all_content(save);
    {std::ofstream output(root/"Sys.sav",std::ios::binary);output.write(reinterpret_cast<const char*>(save.data()),save.size());CHECK(output);}
    m.copy_in(0x521930,std::span(save).subspan(wa2::testing::system_flags_offset,4096));
    const auto path=std::string("C:\\WinRecomp\\Sys.sav");m.copy_in(0x4bf530,{reinterpret_cast<const std::uint8_t*>(path.c_str()),path.size()+1});
    m.store(0xb92638,0xe0000050,32);
    a::Program program(m);a::MusicFlagSaves saves;
    const auto base=p.cpu;const auto stack=base.r[wr::ESP]-512;
    const auto call=[&](U32 flag=131,U32 value=1,U32 caller=0x40fb98){
        p.cpu=base;p.cpu.eip=0x405320;p.cpu.r[wr::ESP]=stack;
        m.store(stack,caller,32);m.store(stack+4,flag,32);m.store(stack+8,value,32);
        unsigned boundaries{};
        while(p.cpu.eip!=caller){CHECK(++boundaries<128);
            std::optional<a::MusicFlagScope> scope;if(optimized)scope.emplace(saves,m,stamp,&p);
            if(!p.dispatch_api())CHECK(program.invoke_unmetered(p.cpu));
        }
        CHECK(p.cpu.r[wr::ESP]==stack+4);
        for(unsigned reg:{unsigned(wr::EBX),unsigned(wr::ESI),unsigned(wr::EDI),unsigned(wr::EBP)})CHECK(p.cpu.r[reg]==base.r[reg]);
        CHECK(m.load(0x521930+flag*4,32)==value && p.executed_instructions==0);++totals.calls;
        return p.cpu.r[wr::EAX];
    };
    for(unsigned n=0;n<120;++n)CHECK(call()==4096);
    CHECK(notifications==120 && backend->opens==(optimized?1u:120u));
    if(optimized)totals.optimized_opens+=backend->opens;else totals.baseline_opens+=backend->opens;
    auto disk=read(root/"Sys.sav");CHECK(disk.size()==save.size());
    std::array<std::uint8_t,4096> flags;m.copy_out(0x521930,flags);
    CHECK(std::equal(disk.begin(),disk.begin()+wa2::testing::system_flags_offset,save.begin()));
    CHECK(std::equal(flags.begin(),flags.end(),disk.begin()+wa2::testing::system_flags_offset));
    if(optimized){
        auto opens=backend->opens;
        m.store(0x521930+1019*4,0x914b81c7,32);CHECK(call()==4096 && backend->opens==++opens);++totals.guards;
        CHECK(call()==4096 && backend->opens==opens);++totals.guards;
        // A different source caller is intentionally never deduplicated.
        CHECK(call(131,1,0xf0001234)==4096 && backend->opens==++opens);++totals.guards;
        CHECK(call()==4096 && backend->opens==++opens);++totals.guards;
        // A failed open, short transfer or failed publication/close retries.
        m.store(0x521930+1016*4,1,32);backend->fail=true;CHECK(call()==0 && backend->opens==++opens);backend->fail=false;
        CHECK(call()==4096 && backend->opens==++opens);CHECK(call()==4096 && backend->opens==opens);++totals.guards;
        m.store(0x521930+1018*4,1,32);short_write=true;CHECK(call()==2048 && backend->opens==++opens);short_write=false;
        CHECK(call()==4096 && backend->opens==++opens);CHECK(call()==4096 && backend->opens==opens);++totals.guards;
        m.store(0x521930+1017*4,1,32);bad_close=true;CHECK(call()==4096 && backend->opens==++opens);bad_close=false;
        CHECK(call()==4096 && backend->opens==++opens);CHECK(call()==4096 && backend->opens==opens);++totals.guards;
        p.state().current_thread_id=2;CHECK(call()==4096 && backend->opens==++opens);p.state().current_thread_id=1;++totals.guards;
        CHECK(call()==4096 && backend->opens==++opens);++totals.guards;
        // A changed selected-directory backend cannot inherit the old stamp.
        p.state().mutable_files=c::save_files(backend,false);CHECK(call()==4096 && backend->opens==++opens);++totals.guards;
        CHECK(call()==4096 && backend->opens==opens);++totals.guards;
        // A second file namespace writes once, preserving the first Sys.sav.
        const auto before=read(root/"Sys.sav");CHECK(std::filesystem::create_directory(root/"Documents"));CHECK(std::filesystem::create_directory(root/"Documents/Leaf"));CHECK(std::filesystem::create_directory(root/"Documents/Leaf/WHITE ALBUM2 -introductory chapter-"));
        const auto other=std::string("C:\\WinRecomp\\Documents\\Leaf\\WHITE ALBUM2 -introductory chapter-\\Sys.sav");
        m.copy_in(0x4bf530,{reinterpret_cast<const std::uint8_t*>(other.c_str()),other.size()+1});
        CHECK(call()==4096 && backend->opens==++opens);CHECK(call()==4096 && backend->opens==opens);
        CHECK(read(root/"Sys.sav")==before && read(root/"Documents/Leaf/WHITE ALBUM2 -introductory chapter-/Sys.sav").size()==wa2::testing::system_save_bytes);++totals.guards;
    }
    totals.notifications+=notifications;++totals.fixtures;
}
int main(int argc,char** argv){try{
    CHECK(argc==2);const auto parent=std::filesystem::absolute(argv[1]).parent_path();const auto root=parent/("music-flags-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));CHECK(std::filesystem::create_directory(root));Totals totals;
    for(bool linear:{false,true})for(bool unlocked:{false,true})for(bool optimized:{false,true})fixture(root/(std::string(linear?"linear":"paged")+(unlocked?"-unlocked":"-normal")+(optimized?"-optimized":"-baseline")),linear,unlocked,optimized,totals);
    CHECK(totals.baseline_opens==480 && totals.optimized_opens==4 && totals.guards==48 && totals.notifications==totals.calls);
    std::ofstream report(argv[1]);report<<"{\"schema\":\"wa2.music-flag-saves.v1\",\"fixtures\":"<<totals.fixtures<<",\"actual_static_setter_calls\":"<<totals.calls<<",\"plugin_notifications\":"<<totals.notifications<<",\"guards\":"<<totals.guards<<",\"baseline_repeated_opens\":480,\"optimized_repeated_opens\":4,\"repeat_calls_per_fixture\":120,\"same_final_flags_and_nonflag_save_bytes\":true,\"failed_open_short_write_and_close_retry\":true,\"other_callers_workers_and_namespaces_preserved\":true,\"whole_game_performance_measured\":false,\"game_ui_exercised\":false,\"game_launched\":false,\"screen_access\":false,\"source_executable_opened\":false,\"passed\":true}\n";CHECK(report);std::cout<<"PASS bounded music flag writes; identical source state and notifications, 480 repeated opens reduced to 4.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
