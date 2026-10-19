// Exercise the original script callback entries through native generated code.
#include "support.hpp"
#include "wa2_win32.hpp"
#include "native_backend.hpp"
#include <winrecomp/compiled.hpp>
#include <fstream>
#include <iostream>

bool code_page_1025(wr::Cpu&,wr::Memory&,std::uint64_t&);
bool code_page_1076(wr::Cpu&,wr::Memory&,std::uint64_t&);
bool code_page_1108(wr::Cpu&,wr::Memory&,std::uint64_t&);
bool code_page_1109(wr::Cpu&,wr::Memory&,std::uint64_t&);
bool code_page_1110(wr::Cpu&,wr::Memory&,std::uint64_t&);
bool code_page_1112(wr::Cpu&,wr::Memory&,std::uint64_t&);
bool code_page_1118(wr::Cpu&,wr::Memory&,std::uint64_t&);
bool code_page_1119(wr::Cpu&,wr::Memory&,std::uint64_t&);
bool code_page_1121(wr::Cpu&,wr::Memory&,std::uint64_t&);
using wr::U32;
#define CHECK(v) do{if(!(v))throw std::runtime_error(#v);}while(false)
static std::vector<std::array<U32,12>> initialization_calls;
static bool step(wr::Cpu& c,wr::Memory& m,std::uint64_t& budget){
    if(c.eip==0x4167e0){
        // Isolate the engine operation invoked by this script command. Record
        // the real generated caller's ABI without creating game windows.
        std::array<U32,12> args{};for(unsigned n=0;n<args.size();++n)args[n]=m.load(c.r[wr::ESP]+4+n*4,32);
        initialization_calls.push_back(args);c.eip=wr::pop(c,m);c.r[wr::EAX]=1;return true;
    }
    switch(c.eip>>12){
    case 0x401:return code_page_1025(c,m,budget);
    case 0x434:return code_page_1076(c,m,budget);
    case 0x454:return code_page_1108(c,m,budget);
    case 0x455:return code_page_1109(c,m,budget);
    case 0x456:return code_page_1110(c,m,budget);
    default:return false;
    }
}
int main(int argc,char** argv){try{
    if(argc!=3)throw std::runtime_error("usage: wa2_script_tests original.exe report.json");
    wr::ProcessOptions options;options.instruction_budget=1000000;
    wr::Process p(step,options);wr::install_wa2_win32(p);p.load(wr::Image::load(argv[1]));
    // This one engine entry is deliberately replaced by the ABI-recording
    // test function above. The script and its real helper still use native
    // generated blocks; the mock must remain a process-dispatch boundary.
    p.state().fast_entries.at(0x4167e0-p.state().fast_base)=0;
    // Cross-page native jumps do not re-enter the process dispatcher. Mark
    // this deliberately replaced code page as changed as well, so they yield
    // before entering the mock instead of running the real engine operation.
    p.memory.initialize(0x4167e0,std::array<std::uint8_t,1>{0xcc});
    CHECK(p.input_sha256=="246a263b965147a274fa59cdd7a72f6be10ba063d8cf4e61e74baefb56f29327");
    const auto context=p.allocate_bytes(4096);const auto saved=p.cpu;unsigned checked=0;
    CHECK(p.memory.load(0x4c25c0+0x13*4,32)==0);
    for(U32 index=0;index<128;++index){
        const auto entry=p.memory.load(0x4c25c0+index*4,32);if(!entry)continue;
        p.cpu=saved;p.cpu.r.fill(context);p.cpu.r[wr::ESP]=saved.r[wr::ESP]-256;p.cpu.eip=entry;
        p.memory.store(p.cpu.r[wr::ESP],0x0badf00d,32);p.memory.store(p.cpu.r[wr::ESP]+4,context,32);
        std::uint64_t budget=10000;
        if(!step(p.cpu,p.memory,budget))throw std::runtime_error("untranslated script callback: "+std::to_string(entry));
        ++checked;
    }
    CHECK(checked==127);
    // The reported crash entry must also execute its complete resumed path,
    // including its real helper at 0x401a30 and cdecl stack cleanup.
    const std::array<U32,1> args{context};
    for(U32 state:{0u,1u,2u,0x80000000u,0xffffffffu}){
        p.cpu=saved;p.memory.store(context,0xabababab,32);p.memory.store(context+4,1,32);
        p.memory.store(0x521784,state,32);
        CHECK(p.callback(0x454f30,args,false)==1);
        CHECK(p.memory.load(context,32)==U32(state!=0) && p.memory.load(context+4,32)==1);
        CHECK(p.cpu.r==saved.r && p.cpu.eip==saved.eip);
    }
    CHECK(initialization_calls.empty());
    const auto scale=wr::port::read<double>(p,0x4a2538),duration=wr::port::read<double>(p,0x4a30f0);
    for(U32 state:{0u,1u,2u,0x80000000u,0xffffffffu}){
        p.cpu=saved;p.memory.store(context+4,0,32);p.memory.store(0x521784,state,32);
        for(const auto [offset,value]:std::array<std::array<U32,2>,7>{{{0x18,11},{0x2c,7},{0x40,3},{0x54,22},{0x68,33},{0x7c,44},{0x90,55}}})p.memory.store(context+offset,value,32);
        wr::port::write(p,context+0xa0,float(scale*0.5));wr::port::write(p,context+0xb4,float(duration*2));
        CHECK(p.callback(0x454f30,args,false)==1);
        CHECK(p.memory.load(context,32)==U32(state!=0) && p.memory.load(context+4,32)==1);
        CHECK(p.cpu.r==saved.r && p.cpu.eip==saved.eip);
        const std::array<U32,12> expected{73,11,0,0,22,33,44,55,std::bit_cast<U32>(0.5f),std::bit_cast<U32>(2.0f),0,0};
        CHECK(initialization_calls.back()==expected);
    }
    CHECK(initialization_calls.size()==5);
    // The user's actual 0x434550 crash is an initially installed cdecl logging
    // callback, called with different varargs by resource and voice loading.
    // Execute both the native block and checked fallback against that RET.
    CHECK(p.memory.load(0x4c227c,32)==0x434550);
    CHECK(p.memory.load(0x434550,8)==0xc3);
    for(U32 flags:{2u,0x8d7u,0x402u,0x897u})for(U32 argc:{1u,2u,3u}){
        auto initial=saved;initial.eip=0x434550;initial.flags=flags;initial.r[wr::EAX]=0x76543210;
        initial.r[wr::ESP]-=64;p.memory.store(initial.r[wr::ESP],0x0badf00d,32);
        for(U32 n=0;n<argc;++n)p.memory.store(initial.r[wr::ESP]+4+n*4,context+n*4,32);
        auto native=initial,checked=initial;std::uint64_t nb=100,cb=100;
        CHECK(wr::wa2_native_batch(native,p.memory,nb,100,0xffffffc0)==1);
        CHECK(code_page_1076(checked,p.memory,cb));
        CHECK(native.eip==0x0badf00d && native.r[wr::ESP]==initial.r[wr::ESP]+4 && nb==99);
        CHECK(native.r==checked.r && native.eip==checked.eip && native.flags==checked.flags);
        CHECK(native.flags==initial.flags && native.r[wr::EAX]==0x76543210);
    }
    for(U32 caller:{0x4589dau,0x45e3d6u,0x45ff7fu,0x461454u,0x46147cu}){
        auto c=saved;c.eip=caller;c.r[wr::ESP]-=64;c.r[wr::EAX]=0x87654321;
        for(U32 n=0;n<3;++n)p.memory.store(c.r[wr::ESP]+n*4,context+n*4,32);
        const auto before=c;std::uint64_t budget=100;
        const auto translated=caller>>12==0x458?code_page_1112(c,p.memory,budget):caller>>12==0x45e?code_page_1118(c,p.memory,budget):caller>>12==0x45f?code_page_1119(c,p.memory,budget):code_page_1121(c,p.memory,budget);
        CHECK(translated && c.eip==0x434550 && c.r[wr::ESP]==before.r[wr::ESP]-4);
        CHECK(wr::wa2_native_batch(c,p.memory,budget,1,caller+6)==1);
        CHECK(c.eip==caller+6 && c.r==before.r && c.flags==before.flags);
    }
    std::ofstream report(argv[2]);report<<"{\"schema\":\"wa2.script-handler-tests.v1\",\"input_sha256\":\""<<p.input_sha256
        <<"\",\"table_slots\":128,\"native_entry_checks\":"<<checked<<",\"former_crash_entry\":4542256,\"completed_resume_cases\":5,\"completed_initialization_cases\":5,\"data_callback_crash_entry\":4408656,\"data_callback_cases\":12,\"real_data_callback_call_sites\":5,\"engine_call_mocked\":true,\"screen_access\":false,\"passed\":true}\n";
    report.close();CHECK(report);
    std::cout<<"PASS script dispatch: 127 native callback entries, reserved null slot, 0x454f30 initialization/resume paths for 10 states, engine call ABI, real helper, cdecl stack and nonvolatile register preservation; no screen access\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
