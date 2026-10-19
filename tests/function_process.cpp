#include "process.hpp"
#include <winrecomp/win32_state.hpp>
#include <winrecomp/resources.hpp>
#include <fstream>
#include <iostream>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
namespace a=wa2::aot;
wr::Image synthetic(){
    wr::Image image;image.base=0x400000;image.size=0x900000;image.entry=0x401300;image.headers_size=4096;
    image.bytes.resize(0xa1000);
    image.sections={{".text",0x1000,0xa0000,0x1000,0xa0000,0x60000020},
                    {".data",0xa1000,image.size-0xa1000,0,0,0xc0000040}};
    for(const auto& f:a::functions())for(const auto& span:f.code)
        std::copy(span.bytes.begin(),span.bytes.end(),image.bytes.begin()+span.address-image.base);
    image.imports={{"WINMM.dll","timeGetTime",0x4a137c,{}}};return image;
}
int main(int argc,char** argv){try{
    CHECK(argc==2);
    // Load only: validate preserved initial RAM and resource directory. This
    // does not enter the game's startup routine, create windows, or play audio.
    const auto original=a::initial_image();
    CHECK(original.static_input_identity=="246a263b965147a274fa59cdd7a72f6be10ba063d8cf4e61e74baefb56f29327");
    CHECK(original.imports.size()==225 && original.sections.size()==4 && original.bytes.size()==1220096);
    const auto resources=wr::resource_directory(original);CHECK(!resources.empty());
    unsigned code_spans{};
    for(const auto& f:a::functions())for(const auto& span:f.code){
        const auto offset=original.offset(span.address,span.bytes.size());CHECK(offset);
        CHECK(std::equal(span.bytes.begin(),span.bytes.end(),original.bytes.begin()+*offset));++code_spans;
    }
    unsigned polls{},services{},nested_callbacks{};
    a::Runtime runtime;auto& process=runtime.process();
    CHECK(process.function_execution() && !process.gui() && !process.directdraw());
    std::vector<std::string> unavailable_imports;
    for(const auto& item:original.imports){const auto name=item.ordinal?"#"+std::to_string(*item.ordinal):item.name;
        if(!process.resolve(item.dll,name))unavailable_imports.push_back(item.dll+"!"+name);}
    process.replace_api("WINMM.dll","timeGetTime",0,[&](wr::Process& p){
        ++services;
        // Re-entry into a compiled callback from a host service must return
        // through the original ABI and restore the enclosing CPU registers.
        const auto saved=p.cpu;
        const auto result=p.callback(0x401260,std::array<wr::U32,1>{7},false);
        CHECK(p.cpu.r==saved.r && p.cpu.eip==saved.eip);++nested_callbacks;return result;
    });
    process.state().poll.push_back([&]{++polls;});
    runtime.load(synthetic());
    process.memory.store(0x4be000,60,32);
    const auto initial_stack=process.cpu.r[wr::ESP];
    process.memory.store(initial_stack+4,5,32);
    CHECK(runtime.run()==12);CHECK(process.memory.load(0xa391ac,32)==12);
    CHECK(process.cpu.r[wr::ESP]==initial_stack+4);
    CHECK(services==1 && nested_callbacks==1 && polls>0 && process.native_function_entries>=3);
    CHECK(process.direct_leaf_api_calls==0); // Replacing timeGetTime revoked its leaf certificate.
    CHECK(process.executed_instructions==0);
    // An unknown function fails without reaching a decoder or a step callback.
    a::Runtime unknown;unknown.load(synthetic());unknown.process().cpu.eip=0xdeadbeef;
    bool rejected=false;try{unknown.run();}catch(const wr::GuestFault& f){rejected=f.kind==wr::FaultKind::untranslated && f.address==0xdeadbeef;}CHECK(rejected);
    std::ofstream report(argv[1]);
    report<<"{\"schema\":\"wa2.shared-function-process.v1\",\"cached_initial_data_bytes\":"<<original.bytes.size()
          <<",\"static_code_spans_verified\":"<<code_spans<<",\"resource_entries\":"<<resources.size()
          <<",\"host_service_calls\":"<<services<<",\"nested_compiled_callbacks\":"<<nested_callbacks
          <<",\"scheduler_polls\":"<<polls<<",\"static_function_entries\":"<<process.native_function_entries
          <<",\"unbound_platform_imports\":[";
    for(unsigned n=0;n<unavailable_imports.size();++n){if(n)report<<',';report<<wr::quote(unavailable_imports[n]);}
    report<<"],\"instruction_step_calls\":0,\"unknown_target_rejected\":true,\"source_executable_opened\":false,"
            "\"screen_access\":false,\"game_launched\":false,\"audio_device_opened\":false,\"complete_gameplay_verified\":false,\"passed\":true}\n";
    std::cout<<"PASS shared function runtime: native calls, host boundary, nested callback, no instruction stepping.\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
