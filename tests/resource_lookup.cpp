// Own initialized resource records only; no PAK, game, UI or device is opened.
#include "process.hpp"
#include "../engine/resource_archive.hpp"
#include <winrecomp/win32_state.hpp>
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iostream>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("resource lookup check at ")+std::to_string(__LINE__)+": "+#x);}while(false)
namespace a=wa2::aot;
using namespace wr;
#if WA2_RESOURCE_REFERENCE
std::unique_ptr<Process> make_resource_reference(bool linear);
#endif
struct Counts {unsigned queries{},diagnostics{},guards{},mutations{};};
Counts exercise(Process& p,const std::function<void()>& load,std::ofstream& snapshot,bool expect_old_error){
    Counts counts;
    U32 manager{},record{},table{},query{};
    const auto string=[&](U32 at,const std::string& value){p.memory.copy_in(at,{reinterpret_cast<const std::uint8_t*>(value.c_str()),value.size()+1});};
    std::vector<std::string> events;bool mutate_prompt{};unsigned prompts{},logs{};
    p.replace_api("user32.dll","MessageBoxA",4,[&](Process& q){
        CHECK(q.argument(0)==0 && q.argument(2)==0x4a6bb8 && q.argument(3)==0x10);
        events.push_back("prompt:"+q.read_string(q.argument(1),1024));++prompts;return 1u;
    });
    p.register_api("resource-fixture","log",3,[&](Process& q){
        CHECK(q.argument(0)==0x4a6bc8 && q.argument(1)==record+10 && q.argument(2)==query);
        events.push_back("log:"+q.read_string(query,1024));++logs;
        if(mutate_prompt)string(query,"changed-by-callback");
        return 0u;
    },true);
    load();manager=p.allocate_bytes(4096);record=p.allocate_bytes(4096);table=p.allocate_bytes(4096);query=p.allocate_bytes(4096);
    p.memory.store(manager+12,record,32);p.memory.store(manager+16,record+0x148,32);p.memory.store(record+0x110,table,32);
    string(record+10,"owned.pak");p.cpu.r[ECX]=manager;
    const auto original_log=p.memory.load(0x4c227c,32);
    const auto initial_files=p.state().files.size();
    p.memory.store(0x4c227c,p.resolve("resource-fixture","log"),32);
    const auto find=[&](const std::vector<std::string>& names,const std::string& name,U32 diagnostic,U32 expected){
        CHECK(names.size()*40<4096);p.memory.store(record+4,U32(names.size()),32);
        for(unsigned n=0;n<names.size();++n){std::array<std::uint8_t,40> row{};CHECK(names[n].size()<31);std::copy(names[n].begin(),names[n].end(),row.begin());p.memory.copy_in(table+n*40,row);}
        string(query,name);events.clear();const auto before=p.cpu;const auto old_logs=logs,old_prompts=prompts;
        const auto result=p.callback(0x4588a0,std::array<U32,3>{0,query,diagnostic});
        CHECK(result==expected && p.cpu.r==before.r && p.cpu.eip==before.eip);
        const bool missing=expected==0xffffffffu;
        CHECK(logs-old_logs==unsigned(missing && diagnostic) && prompts-old_prompts==unsigned(missing && diagnostic));
        if(missing && diagnostic){CHECK(events.size()==2 && events[0]=="log:"+name && events[1]=="prompt:"+(mutate_prompt?std::string("changed-by-callback"):name));++counts.diagnostics;}
        snapshot<<names.size()<<':'<<name<<':'<<diagnostic<<':'<<result<<'\n';for(const auto& event:events)snapshot<<event<<'\n';++counts.queries;
    };
    if(expect_old_error){
        bool reproduced{};try{find({"present.bin"},"present.bin",0,0);}catch(const GuestFault& fault){reproduced=fault.kind==FaultKind::untranslated && fault.address==0x4588a0;}
        CHECK(reproduced);counts.guards=1;return counts;
    }
    for(unsigned size=1;size<=18;++size){
        std::vector<std::string> names;for(unsigned n=0;n<size;++n){char text[32];std::snprintf(text,sizeof(text),"item%03u.bin",n);names.emplace_back(text);}
        for(unsigned n=0;n<size;++n){auto upper=names[n];for(auto& c:upper)if(c>='a' && c<='z')c-=32;find(names,upper,n%2,n);}
        for(const auto* name:{"","before.bin","item999.bin"})for(U32 diagnostic:{0u,1u})find(names,name,diagnostic,0xffffffffu);
    }
    // Retain the source midpoint, including duplicates reached from below.
    find({"a","b","c","dup","dup"},"DUP",0,3);
    find({"ab","abc","abcd"},"abc",1,1);
    find({"ab","abc","abcd"},"abcde",1,0xffffffffu);
    const std::string sjis="\x82\xa0.bin";find({"a",sjis},sjis,1,1);
    find({},"empty.bin",1,0xffffffffu);
    // The logging callback may change the text before the source prompt reads it.
    mutate_prompt=true;find({"a"},"missing.bin",1,0xffffffffu);mutate_prompt=false;++counts.mutations;
    find({"old.bin"},"old.bin",0,0);find({"new.bin"},"new.bin",0,0);++counts.mutations;
    // No original instruction-level resource body is required on these paths.
    CHECK(p.state().files.size()==initial_files && p.executed_instructions==0);
    // Invoke the actual initialized logging callback as well; MessageBox remains
    // intercepted, and its message must still be the requested resource name.
    p.memory.store(0x4c227c,original_log,32);string(query,"default-callback.bin");
    const auto old_prompts=prompts;p.memory.store(record+4,1,32);
    CHECK(p.callback(0x4588a0,std::array<U32,3>{0,query,1})==0xffffffffu && prompts==old_prompts+1);++counts.diagnostics;
    p.memory.store(0x4c227c,p.resolve("resource-fixture","log"),32);
#if !WA2_RESOURCE_REFERENCE
    const auto rejects=[&](U32 archive,FaultKind kind){const auto saved=p.cpu;bool rejected{};
        try{p.callback(0x4588a0,std::array<U32,3>{archive,query,0});}catch(const GuestFault& fault){rejected=fault.kind==kind;}
        p.cpu=saved;CHECK(rejected);++counts.guards;
    };
    rejects(1,FaultKind::unsupported);rejects(0xffffffffu,FaultKind::unsupported);
    p.memory.store(record+0x110,0xfffffff0u,32);rejects(0,FaultKind::memory);p.memory.store(record+0x110,table,32);
    p.memory.store(manager+16,record+1,32);rejects(0,FaultKind::unsupported);p.memory.store(manager+16,record+0x148,32);
    const auto saved=p.cpu;p.memory.store(0x4c227c,0xdeadbeefu,32);bool unknown{};
    try{p.callback(0x4588a0,std::array<U32,3>{0,query,1});}catch(const GuestFault& fault){unknown=fault.kind==FaultKind::untranslated && fault.address==0xdeadbeef;}
    p.cpu=saved;CHECK(unknown);++counts.guards;
#endif
    p.memory.store(0x4c227c,original_log,32);return counts;
}
int main(int argc,char** argv){try{
    CHECK(argc==2 || (argc==3 && std::string_view(argv[2])=="--expect-old-error"));Counts total;
    std::ofstream snapshot(std::string(argv[1])+".snapshot");
    for(bool linear:{false,true}){Counts c;
#if WA2_RESOURCE_REFERENCE
        auto p=make_resource_reference(linear);c=exercise(*p,[&]{p->load(a::initial_image());},snapshot,false);
#else
        ProcessOptions options;options.prefer_linear_memory=linear;a::Runtime runtime(options);c=exercise(runtime.process(),[&]{runtime.load();},snapshot,argc==3);
#endif
        total.queries+=c.queries;total.diagnostics+=c.diagnostics;total.guards+=c.guards;total.mutations+=c.mutations;
    }
    std::ofstream report(argv[1]);report<<"{\"queries\":"<<total.queries<<",\"diagnostics\":"<<total.diagnostics<<",\"guards\":"<<total.guards<<",\"mutations\":"<<total.mutations
        <<",\"paged_and_linear\":true,\"no_open_archive_required\":true,\"old_error_reproduced\":"<<(argc==3?"true":"false")<<",\"instruction_step_calls\":0,\"game_launched\":false,\"screen_access\":false,\"source_executable_opened\":false,\"passed\":true}\n";
    CHECK(report && snapshot);std::cout<<"PASS owned-memory resource directory lookup and missing-file diagnostics.\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
