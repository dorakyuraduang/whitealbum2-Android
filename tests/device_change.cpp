// Synthetic device notifications to one hidden owned window; no input/pixels.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dbt.h>
#include <winrecomp/process.hpp>
#include "wa2_win32.hpp"
#include "support.hpp"
#include <cstring>
#include <fstream>
#include <iostream>
using namespace wr;
static Process* active;static unsigned checks=0,notifications=0;
static bool verify=false,forward_default=false,mutate=false;
static U32 expected_event=0,answer=1;static std::vector<std::uint8_t> expected;
#define CHECK(x) do{++checks;if(!(x))throw std::runtime_error(#x);}while(false)
static U32 call(const char* name,std::initializer_list<U32> args){return active->callback(active->resolve("user32.dll",name),std::span(args.begin(),args.size()));}
static bool step(Cpu& cpu,Memory& memory,std::uint64_t&){
    if(cpu.eip!=active->entry)return false;
    const auto sp=cpu.r[ESP],message=memory.load(sp+8,32);
    if(message!=WM_DEVICECHANGE || !verify || memory.load(sp+12,32)!=expected_event){cpu.eip=active->resolve("user32.dll","DefWindowProcA");return true;}
    const auto lp=memory.load(sp+16,32);++notifications;
    if(expected.empty())CHECK(lp==0);else{
        CHECK(lp);std::vector<std::uint8_t> actual(expected.size());memory.copy_out(lp,actual);CHECK(actual==expected);
        if(mutate)memory.store(lp+U32(expected.size()-1),0x7f,8);
    }
    if(forward_default){cpu.eip=active->resolve("user32.dll","DefWindowProcA");return true;}
    cpu.r[EAX]=answer;cpu.eip=pop(cpu,memory);cpu.r[ESP]+=16;return true;
}
int main(int argc,char** argv){try{
    CHECK(argc==3);ProcessOptions options;options.instruction_budget=1000000;Process p(step,options);active=&p;install_wa2_win32(p);p.load(Image::load(argv[1]));
    p.state().fast_step=nullptr;p.state().block_step=nullptr;p.state().fast_entries.clear();
    const auto buffer=p.allocate_bytes(4096),klass=p.put_string("WA2 synthetic device boundary");
    port::write(p,buffer,std::array<U32,10>{0,p.entry,0,0,p.image_base,0,0,0,0,klass});CHECK(call("RegisterClassA",{buffer}));
    const auto window=call("CreateWindowExA",{0,klass,klass,WS_OVERLAPPED,0,0,32,32,0,0,p.image_base,0});CHECK(window);
    const auto native=reinterpret_cast<HWND>(p.gui()->native_window(window));
    auto invoke=[&](const char* name,std::initializer_list<U32> args){const auto saved=p.cpu;try{return call(name,args);}catch(...){p.cpu=saved;throw;}};
    auto health=[&]{CHECK(invoke("GetSystemMetrics",{0})>0);};
    verify=true;
    for(auto event:{DBT_DEVNODES_CHANGED,DBT_QUERYCHANGECONFIG,DBT_CONFIGCHANGED,DBT_CONFIGCHANGECANCELED}){
        expected_event=event;expected.clear();answer=event==DBT_QUERYCHANGECONFIG?BROADCAST_QUERY_DENY:1;
        CHECK(U32(::SendMessageA(native,WM_DEVICECHANGE,event,0))==answer);health();
        CHECK(invoke("SendMessageA",{window,WM_DEVICECHANGE,U32(event),0})==answer);
        const auto baseline=U32(::DefWindowProcA(native,WM_DEVICECHANGE,event,0));
        CHECK(invoke("DefWindowProcA",{window,WM_DEVICECHANGE,U32(event),0})==baseline);
    }
    expected_event=DBT_DEVNODES_CHANGED;expected.clear();answer=1;
    CHECK(invoke("PostMessageA",{window,WM_DEVICECHANGE,DBT_DEVNODES_CHANGED,0}));
    CHECK(invoke("PeekMessageA",{buffer,window,WM_DEVICECHANGE,WM_DEVICECHANGE,PM_NOREMOVE})==1);
    CHECK(p.memory.load(buffer+8,32)==DBT_DEVNODES_CHANGED && p.memory.load(buffer+12,32)==0);
    CHECK(invoke("GetMessageA",{buffer,window,WM_DEVICECHANGE,WM_DEVICECHANGE})==1);
    CHECK(invoke("DispatchMessageA",{buffer})==answer);
    for(auto type:{DBT_DEVTYP_OEM,1,DBT_DEVTYP_VOLUME,DBT_DEVTYP_PORT,DBT_DEVTYP_DEVICEINTERFACE}){
        expected.assign(type==1?16:type==DBT_DEVTYP_DEVICEINTERFACE?64:type==DBT_DEVTYP_PORT?32:20,0);
        auto word=[&](unsigned offset,U32 value){std::memcpy(expected.data()+offset,&value,4);};
        word(0,U32(expected.size()));word(4,type);word(12,0x12345678);
        if(type==DBT_DEVTYP_PORT)std::memcpy(expected.data()+12,"COM_TEST",9);
        if(type==DBT_DEVTYP_DEVICEINTERFACE)std::memcpy(expected.data()+28,"WA2-synthetic-device",21);
        const auto pristine=expected;expected_event=DBT_DEVICEARRIVAL;answer=1;mutate=true;
        CHECK(U32(::SendMessageA(native,WM_DEVICECHANGE,expected_event,reinterpret_cast<LPARAM>(expected.data())))==1);health();CHECK(expected==pristine);
        p.memory.copy_in(buffer+256,expected);CHECK(invoke("SendMessageA",{window,WM_DEVICECHANGE,expected_event,buffer+256})==1);
        std::vector<std::uint8_t> after(expected.size());p.memory.copy_out(buffer+256,after);CHECK(after==expected);mutate=false;
        const auto baseline=U32(::DefWindowProcA(native,WM_DEVICECHANGE,expected_event,reinterpret_cast<LPARAM>(expected.data())));
        forward_default=true;CHECK(U32(::SendMessageA(native,WM_DEVICECHANGE,expected_event,reinterpret_cast<LPARAM>(expected.data())))==baseline);health();forward_default=false;
        CHECK(invoke("DefWindowProcA",{window,WM_DEVICECHANGE,expected_event,buffer+256})==baseline);
        CHECK(!invoke("PostMessageA",{window,WM_DEVICECHANGE,expected_event,buffer+256}) && p.last_error==ERROR_MESSAGE_SYNC_ONLY);
    }
    for(auto [size,type]:{std::pair<U32,U32>{11,DBT_DEVTYP_VOLUME},{65536,DBT_DEVTYP_VOLUME},{64,DBT_DEVTYP_HANDLE},{24,DBT_DEVTYP_VOLUME},{24,DBT_DEVTYP_OEM}}){
        port::write(p,buffer+256,std::array<U32,3>{size,type,0});bool refused=false;
        try{invoke("SendMessageA",{window,WM_DEVICECHANGE,DBT_DEVICEARRIVAL,buffer+256});}catch(const GuestFault& error){refused=error.kind==FaultKind::unsupported;}
        CHECK(refused);
    }
    port::write(p,buffer,std::array<U32,7>{window,WM_DEVICECHANGE,DBT_DEVICEARRIVAL,buffer+256,0,0,0});
    bool queued_pointer_refused=false;
    try{invoke("DispatchMessageA",{buffer});}catch(const GuestFault& error){queued_pointer_refused=error.kind==FaultKind::unsupported;}
    CHECK(queued_pointer_refused);
    verify=false;CHECK(invoke("DestroyWindow",{window}));
    std::ofstream report(argv[2]);report<<"{\"schema\":\"wa2.device-message-tests.v1\",\"assertions\":"<<checks<<",\"notifications\":"<<notifications<<",\"hidden_owned_window\":true,\"screen_access\":false,\"automated_input\":false,\"passed\":true}\n";CHECK(report);
    std::cout<<"PASS "<<checks<<" device-message assertions: scalar queue/dispatch, 5 pointer-free layouts, input-copy lifetime, DefWindowProc, query denial and malformed payloads; no input or screen access\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
