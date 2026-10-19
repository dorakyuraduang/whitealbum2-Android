// Wait-object semantics and bounded CPU/allocation measurements, no game/UI.
#include <winrecomp/process.hpp>
#include "../platform/compat/scheduler.hpp"
#include <array>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <new>
namespace {thread_local bool counting{};thread_local std::uint64_t allocations{};}
void* operator new(std::size_t bytes){if(counting)++allocations;if(auto* p=std::malloc(bytes?bytes:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t bytes){return ::operator new(bytes);}
void operator delete(void* p)noexcept{std::free(p);}void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed at ")+std::to_string(__LINE__)+": "+#x);}while(false)
using namespace wr;
int main(int argc,char** argv){try{
    CHECK(argc==2);Process p(FunctionExecutor{[](Cpu&,Memory&){throw std::runtime_error("unexpected code execution");return false;}});wa2::platform::compat::install_scheduler(p);
    Image image;image.base=0x400000;image.size=0x10000;image.headers_size=4096;image.entry=0x401000;image.bytes.resize(8192,0x90);image.sections={{".text",4096,4096,4096,4096,0x60000020}};p.load(image);
    const auto api=[&](const char* name,std::initializer_list<U32> args){return p.callback(p.resolve("kernel32.dll",name),std::span(args.begin(),args.size()));};
    const auto scratch=p.allocate_bytes(4096);unsigned checks{};
    auto multi=[&](std::span<const U32> handles,bool all){p.memory.copy_in(scratch,{reinterpret_cast<const std::uint8_t*>(handles.data()),handles.size_bytes()});return api("WaitForMultipleObjects",{U32(handles.size()),scratch,U32(all),0});};
    const auto automatic=api("CreateEventA",{0,0,1,0}),manual=api("CreateEventA",{0,1,1,0}),off=api("CreateEventA",{0,1,0,0}),mutex=api("CreateMutexA",{0,1,0});CHECK(automatic && manual && off && mutex);
    CHECK(multi(std::array{off,manual,automatic},false)==1);CHECK(api("WaitForSingleObject",{automatic,0})==0);++checks;
    CHECK(api("SetEvent",{automatic}));CHECK(multi(std::array{automatic,off,mutex},true)==258);CHECK(api("WaitForSingleObject",{automatic,0})==0);++checks;
    CHECK(api("SetEvent",{automatic}));CHECK(api("SetEvent",{off}));CHECK(multi(std::array{automatic,off,mutex},true)==0);CHECK(api("WaitForSingleObject",{automatic,0})==258);
    CHECK(api("ReleaseMutex",{mutex})==1 && api("ReleaseMutex",{mutex})==1 && api("ReleaseMutex",{mutex})==0);++checks;
    CHECK(api("SetEvent",{automatic}));CHECK(multi(std::array{automatic,0xdeadbeefu},false)==0xffffffffu && p.last_error==6);CHECK(api("WaitForSingleObject",{automatic,0})==0);++checks;
    CHECK(api("SetEvent",{automatic}));CHECK(multi(std::array{automatic,automatic},false)==0xffffffffu && p.last_error==87);CHECK(api("WaitForSingleObject",{automatic,0})==0);++checks;
    CHECK(api("WaitForMultipleObjects",{0,0xdeadbeefu,0,0})==0xffffffffu && p.last_error==87);CHECK(api("WaitForMultipleObjects",{65,0xdeadbeefu,0,0})==0xffffffffu && p.last_error==87);checks+=2;
    std::array<U32,64> large;for(auto& handle:large)handle=api("CreateEventA",{0,1,0,0});
    CHECK(api("SetEvent",{large.back()}));CHECK(multi(large,false)==63);CHECK(multi(large,true)==258);checks+=2;
    for(auto handle:large)CHECK(api("SetEvent",{handle}));CHECK(multi(large,true)==0);++checks;
    std::reverse(large.begin(),large.end());CHECK(multi(large,false)==0);++checks;
    const auto saved=p.cpu;bool memory_fault{};try{api("WaitForMultipleObjects",{2,0xdeadbeefu,0,0});}catch(const GuestFault& e){memory_fault=e.kind==FaultKind::memory && e.address==0xdeadbeefu;}CHECK(memory_fault);p.cpu=saved;++checks;
    // Requested nonzero sleeps retain their real deadline. Only this synthetic
    // API call waits for time; no game timer or skip logic is altered.
    const auto begin=std::chrono::steady_clock::now();api("Sleep",{8});const auto sleep_ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-begin).count();CHECK(sleep_ns>=8000000);++checks;
    const auto one=p.resolve("kernel32.dll","WaitForSingleObject"),many=p.resolve("kernel32.dll","WaitForMultipleObjects");
    constexpr unsigned Repetitions=512;std::array<std::array<std::uint64_t,5>,4> ns{},allocs{};
    for(unsigned kind=0;kind<4;++kind){
        const std::array<U32,3> small{off,manual,large.front()};if(kind<3)p.memory.copy_in(scratch,{reinterpret_cast<const std::uint8_t*>(small.data()),sizeof(small)});
        else p.memory.copy_in(scratch,{reinterpret_cast<const std::uint8_t*>(large.data()),sizeof(large)});
        const std::array<U32,2> single{manual,0};const std::array<U32,4> multiple{kind==3?64u:3u,scratch,kind==2?1u:0u,0};
        auto call=[&]{return kind==0?p.callback(one,single):p.callback(many,multiple);};CHECK(call()==0);
        for(unsigned round=0;round<5;++round){allocations=0;const auto start=std::chrono::steady_clock::now();counting=true;
            for(unsigned n=0;n<Repetitions;++n)if(call()!=0)throw std::runtime_error("wait measurement result changed");
            counting=false;ns[kind][round]=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();allocs[kind][round]=allocations;}
    }
    CHECK(!p.executed_instructions);for(auto handle:large)CHECK(api("CloseHandle",{handle}));for(auto handle:{automatic,manual,off,mutex})CHECK(api("CloseHandle",{handle}));
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.wait-probes.v1\",\"semantic_cases\":"<<checks<<",\"nonzero_sleep_ns\":"<<sleep_ns<<",\"timings\":[";
    for(unsigned kind=0;kind<4;++kind){if(kind)out<<',';out<<"{\"kind\":"<<kind<<",\"repetitions\":"<<Repetitions<<",\"ns\":[";for(unsigned n=0;n<5;++n){if(n)out<<',';out<<ns[kind][n];}out<<"],\"heap_allocations\":[";for(unsigned n=0;n<5;++n){if(n)out<<',';out<<allocs[kind][n];}out<<"]}";}
    out<<"],\"original_resources_opened\":false,\"source_executable_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";
    std::cout<<"PASS wait probes: "<<checks<<" semantic cases; bounded single/multiple-object CPU and allocation measurements.\n";
}catch(const std::exception& e){counting=false;std::cerr<<e.what()<<'\n';return 1;}}
