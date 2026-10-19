// One or two bounded weather-function calls per fixture, no game/frame loop or device.
#include "process.hpp"
#include "native_math.hpp"
#include "uv_clip.hpp"
#if WA2_PARTICLE_WORK_DIAGNOSTICS
#include "work_samples.hpp"
#include "native_weather.hpp"
#endif
#include <winrecomp/hash.hpp>
#include <bit>
#include <chrono>
#include <cfenv>
#include <cerrno>
#include <fstream>
#include <iostream>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
namespace a=wa2::aot;
int main(int argc,char** argv){try{
    bool work_only{},component_only{},render_reference{};
#if WA2_PARTICLE_WORK_DIAGNOSTICS
    work_only=argc==3 && std::string(argv[2])=="--work";
    component_only=argc==3 && std::string(argv[2])=="--components";
    render_reference=argc==3 && std::string(argv[2])=="--render-reference";
    if(render_reference)a::native_render_math_enabled=false;
#endif
    CHECK(argc==2 || work_only || component_only || render_reference);a::Runtime runtime;runtime.load();auto& p=runtime.process();a::Program program(p.memory);
    p.memory.store(0xbc830c,1,32);auto base=p.cpu;base.r[wr::ESP]-=0x4000;base.fp.put16(0,0x7f);
    const auto setf=[&](unsigned at,float value){p.memory.store(at,std::bit_cast<std::uint32_t>(value),32);};
    const std::vector<std::uint8_t> zero(0x80000),zero_tweens(2048*92);
    const auto enter=[&]{p.cpu=base;p.cpu.eip=0x4078b0;p.memory.store(base.r[wr::ESP],0xf0001234,32);};
    const auto prepare=[&](unsigned count){
        p.memory.copy_in(base.r[wr::ESP]-0x100,std::array<std::uint8_t,0x200>{});
        p.memory.copy_in(0xab3fb8,zero);
        p.memory.copy_in(0xb4fd58,zero_tweens);
        for(auto [at,value]:{std::pair{0x522930u,1u},{0x522934u,3u},{0x522938u,0u},{0x522940u,1u},
            {0x522944u,0u},{0x5267d4u,count},{0x5267e4u,count},{0x5267d8u,0u},{0x5267e8u,0u},{0x5267ecu,1u},
            {0x5267dcu,1u},{0x5267e0u,3u}})p.memory.store(at,value,32);
        setf(0x5267cc,1);setf(0x5267d0,3);
        const auto image=0xa93fb8+0x6e*128;p.memory.store(image+0x5c,32,16);p.memory.store(image+0x5e,64,16);
        for(unsigned n=0;n<count;++n){const auto at=0x522948+n*40;
            p.memory.store(at,1,16);p.memory.store(at+2,248+n%8,16);p.memory.store(at+4,256,16);p.memory.store(at+8,n%256,16);
            setf(at+12,float(10+n%1000));setf(at+16,float(10+n%600));setf(at+20,float(248+n%8));
            setf(at+24,1.25f);setf(at+28,1.5f);}
        enter();
    };
    const auto execute=[&](bool metered){
        a::NativeMathReferenceScope numeric;std::uint64_t budget=5000000;unsigned boundaries{};
        const auto start=std::chrono::steady_clock::now();
        while(p.cpu.eip!=0xf0001234){CHECK(++boundaries<64);CHECK(metered?program.invoke(p.cpu,budget):program.invoke_unmetered(p.cpu));}
        return std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count());
    };
    const auto sequence=[&](bool metered,unsigned updates){std::uint64_t elapsed{};
        for(unsigned n=0;n<updates;++n){if(n)enter();elapsed+=execute(metered);}return elapsed;};
    const auto snapshot=[&]{std::vector<std::uint8_t> bytes(0x5000+0x80000+zero_tweens.size()+0x200);p.memory.copy_out(0x522000,std::span(bytes).first(0x5000));
        p.memory.copy_out(0xab3fb8,std::span(bytes).subspan(0x5000,0x80000));
        p.memory.copy_out(0xb4fd58,std::span(bytes).subspan(0x85000,zero_tweens.size()));
        p.memory.copy_out(base.r[wr::ESP]-0x100,std::span(bytes).last(0x200));return bytes;};
#if WA2_PARTICLE_WORK_DIAGNOSTICS
    if(component_only){
        prepare(400);std::ofstream out(argv[1]);out<<"{\"components\":[";unsigned cases{};
        for(unsigned vectors:{0,1})for(unsigned entry:{0x474f00u,0x475430u,0x449870u,0x449a00u,0x43a7d0u,0x449a30u,0x43a850u})for(bool enabled:{false,true}){
            p.memory.store(0xbc8310,vectors,32);a::native_render_math_enabled=enabled;std::array<std::uint64_t,128> times{};
            for(auto& ns:times){
                p.cpu=base;p.cpu.eip=entry;const auto stack=base.r[wr::ESP];p.memory.store(stack,0xf0001234,32);
                for(auto [offset,value]:{std::pair{4u,0u},{8u,110u},{12u,1u},{16u,1u}})p.memory.store(stack+offset,value,32);
                if(entry==0x474f00){
                    const auto value=a::fp::from_integer(21);p.cpu.fp.put64(32,value.significand);p.cpu.fp.put16(40,value.sign_exponent);p.cpu.fp.bytes[4]=1;
                }else if(entry==0x475430){p.memory.store(stack+4,0xab3fb8,32);p.memory.store(stack+8,0,32);p.memory.store(stack+12,256,32);
                }else if(entry!=0x449870){for(auto [offset,value]:{std::pair{8u,10.25f},{12u,15.5f},{16u,32.25f},{20u,64.75f}})setf(stack+offset,value);}
                ns=execute(false);
            }
            if(cases++)out<<',';out<<"{\"entry\":"<<entry<<",\"vectors\":"<<vectors<<",\"native_render_math_enabled\":"<<enabled<<",\"elapsed_ns\":[";
            for(unsigned n=0;n<times.size();++n){if(n)out<<',';out<<times[n];}out<<"]}";
        }
        a::native_render_math_enabled=true;out<<"],\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";return 0;
    }
    if(work_only){
        std::ofstream out(argv[1]);out<<"{\"cases\":[";unsigned cases{};
        for(unsigned count:{16,400})for(unsigned vectors:{0,1})for(unsigned updates:{1,2}){
            p.memory.store(0xbc8310,vectors,32);prepare(count);a::FunctionWorkSamples work(a::functions());
            {a::FunctionWorkScope scope(&work);sequence(false,updates);}
            if(cases++)out<<',';out<<"{\"particles\":"<<count<<",\"vectors\":"<<vectors<<",\"updates\":"<<updates<<",\"work\":"<<work.report()<<'}';
        }
        out<<"],\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";return 0;
    }
#endif
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.bounded-weather-cpu.v1\",\"cases\":[";unsigned cases{};
    for(unsigned updates:{1,2})for(unsigned vectors:{0,1})for(unsigned count:{16,64,128,400}){
        p.memory.store(0xbc8310,vectors,32);
        prepare(count);sequence(true,updates);const auto reference_cpu=p.cpu;const auto reference=snapshot();
        prepare(count);sequence(false,updates);CHECK(reference_cpu.r==p.cpu.r && reference_cpu.fp.bytes==p.cpu.fp.bytes && reference_cpu.flags==p.cpu.flags && reference_cpu.defined_flags==p.cpu.defined_flags);
        CHECK(reference==snapshot());std::array<std::uint64_t,8> times{};
        for(auto& ns:times){prepare(count);ns=sequence(false,updates);CHECK(reference_cpu.r==p.cpu.r && reference_cpu.fp.bytes==p.cpu.fp.bytes && reference==snapshot());}
        if(cases++)out<<',';out<<"{\"particles\":"<<count<<",\"updates\":"<<updates<<",\"vector_clear_enabled\":"<<vectors<<",\"ram_sha256\":\""<<wr::sha256(reference)<<"\",\"elapsed_ns\":[";
        for(unsigned n=0;n<times.size();++n){if(n)out<<',';out<<times[n];}out<<"],\"exact_cpu_fp_and_ram\":true}";
    }
    unsigned prefix_cases{},environment_cases{},control_cases{},decline_cases{};
#if WA2_PARTICLE_WORK_DIAGNOSTICS
    if(!render_reference){
    const auto one_instruction=[&]{std::uint64_t budget=1;
        if(p.cpu.eip==0x47548f || p.cpu.eip==0x47e822 || p.cpu.eip==0x47e839){CHECK(p.cpu.r[wr::ECX]<4096);budget+=p.cpu.r[wr::ECX];}
        try{CHECK(program.invoke(p.cpu,budget));}
        catch(const wr::GuestFault& fault){CHECK(fault.kind==wr::FaultKind::budget);}};
    const auto prefix_prepare=[&](unsigned reason,unsigned vectors){
        p.memory.store(0xbc8310,vectors,32);prepare(128);unsigned steps{};
        while(p.cpu.eip!=0x4088cf){CHECK(++steps<100000);one_instruction();}
        if(reason<2){const auto sprite=0xab3fb8+81*256;
            p.memory.store(sprite,1,8);p.memory.store(sprite+0x14,reason==0?1:0,8);
            p.memory.store(sprite+0x4a,0,16);p.memory.store(sprite+0x10,reason==1?0x22222222:0,32);
        }else{setf(0x522948+3*40+12,10000);setf(0x522948+3*40+16,10000);}
    };
    for(unsigned vectors:{0,1})for(unsigned reason:{0,1,2}){
        prefix_prepare(reason,vectors);unsigned steps{};
        const auto stop=[&]{return reason==0?p.cpu.eip==0x43980c:reason==1?p.cpu.eip==0x439821:
            p.cpu.eip==0x408a70 || p.cpu.eip==0x408ad5 || p.cpu.eip==0x408b3e;};
        while(!stop()){CHECK(p.cpu.eip!=0x408f5b && p.cpu.eip!=0x409c60);CHECK(++steps<500000);one_instruction();}
        const auto reference_cpu=p.cpu;const auto reference=snapshot();
        prefix_prepare(reason,vectors);CHECK(a::native_weather_batch(p.cpu,p.memory));
        CHECK(reference_cpu.r==p.cpu.r && reference_cpu.eip==p.cpu.eip && reference_cpu.fp.bytes==p.cpu.fp.bytes &&
            reference_cpu.flags==p.cpu.flags && reference_cpu.defined_flags==p.cpu.defined_flags);
        CHECK(reference==snapshot());++prefix_cases;
    }
    for(unsigned reason:{0,1,2,3,4,5}){
        prefix_prepare(2,0);
        if(reason==0)p.memory.store(0x5267d4,401,32);
        if(reason==1)p.cpu.flags|=wr::DF;
        if(reason==2)p.cpu.fp.bytes[4]=1;
        if(reason==3)p.cpu.fp.put16(0,0x7e);
        if(reason==4)p.cpu.r[wr::ESP]=0x522100;
        if(reason==5)p.memory.protect(0xab9000,4096,wr::Memory::Read);
        const auto reference_cpu=p.cpu;const auto reference=snapshot();
        CHECK(!a::native_weather_batch(p.cpu,p.memory));
        CHECK(reference_cpu.r==p.cpu.r && reference_cpu.eip==p.cpu.eip && reference_cpu.fp.bytes==p.cpu.fp.bytes &&
            reference_cpu.flags==p.cpu.flags && reference_cpu.defined_flags==p.cpu.defined_flags);CHECK(reference==snapshot());
        if(reason==5)p.memory.protect(0xab9000,4096,3);
        ++decline_cases;
    }
    for(unsigned vectors:{0,1})for(unsigned control:{0x27fu,0x37fu,0x47fu,0x87fu,0xc7fu}){
        p.memory.store(0xbc8310,vectors,32);base.fp.put16(0,std::uint16_t(control));
        prepare(128);sequence(true,2);const auto reference_cpu=p.cpu;const auto reference=snapshot();
        prepare(128);sequence(false,2);CHECK(reference_cpu.r==p.cpu.r && reference_cpu.eip==p.cpu.eip && reference_cpu.fp.bytes==p.cpu.fp.bytes &&
            reference_cpu.flags==p.cpu.flags && reference_cpu.defined_flags==p.cpu.defined_flags);CHECK(reference==snapshot());++control_cases;
    }
    base.fp.put16(0,0x7f);
    {
        struct Restore {std::fenv_t state;int error{errno};~Restore(){std::fesetenv(&state);errno=error;}} restore;
        CHECK(!std::feholdexcept(&restore.state));
        for(int rounding:{FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO}){
            CHECK(!std::fesetround(rounding));CHECK(!std::feclearexcept(FE_ALL_EXCEPT));CHECK(!std::feraiseexcept(FE_INEXACT|FE_DIVBYZERO));errno=ERANGE;
            const auto exceptions=std::fetestexcept(FE_ALL_EXCEPT);prepare(128);sequence(false,2);
            CHECK(std::fegetround()==rounding && std::fetestexcept(FE_ALL_EXCEPT)==exceptions && errno==ERANGE);++environment_cases;
        }
    }
    }
#endif
    constexpr wr::U32 Buffer=0x2600000;constexpr unsigned Capacity=262144;p.memory.map(Buffer,Capacity,3);
    const std::vector<std::uint8_t> untouched(Capacity,0xa5);
    const auto prepare_buffer=[&](unsigned bytes,unsigned vectors,unsigned offset,unsigned value,unsigned fault_mode=0){
        p.memory.protect(Buffer,Capacity,3);p.memory.copy_in(Buffer,untouched);p.memory.store(0xbc8310,vectors,32);
        p.memory.copy_in(base.r[wr::ESP]-0x80,std::array<std::uint8_t,0x100>{});
        p.cpu=base;p.cpu.eip=0x475430;
        for(auto [offset_at,word]:{std::pair{0u,0xf0001234u},{4u,Buffer+offset},{8u,value},{12u,bytes}})
            p.memory.store(base.r[wr::ESP]+offset_at,word,32);
        if(fault_mode==1)p.memory.protect(Buffer+4096,4096,wr::Memory::Read);
        if(fault_mode==2)p.cpu.fp.put32(24,0x10000|0x1f80);
    };
    const auto buffer_snapshot=[&]{
        p.memory.protect(Buffer,Capacity,3);std::vector<std::uint8_t> bytes(Capacity+0x100);
        p.memory.copy_out(Buffer,std::span(bytes).first(Capacity));p.memory.copy_out(base.r[wr::ESP]-0x80,std::span(bytes).last(0x100));return bytes;
    };
    const auto equal_cpu=[&](const wr::Cpu& reference){
        CHECK(reference.r==p.cpu.r && reference.eip==p.cpu.eip && reference.fp.bytes==p.cpu.fp.bytes && reference.flags==p.cpu.flags && reference.defined_flags==p.cpu.defined_flags);
        CHECK(reference.selectors==p.cpu.selectors && reference.fs_base==p.cpu.fs_base && reference.gs_base==p.cpu.gs_base && reference.fs_valid==p.cpu.fs_valid && reference.gs_valid==p.cpu.gs_valid);
    };
    out<<"],\"buffer_initialization\":[";cases=0;
    for(unsigned vectors:{0,1})for(unsigned bytes:{256,8192,192000})for(unsigned offset:{0,3}){
        prepare_buffer(bytes,vectors,offset,0);execute(true);const auto reference_cpu=p.cpu;const auto reference=buffer_snapshot();
        prepare_buffer(bytes,vectors,offset,0);execute(false);equal_cpu(reference_cpu);CHECK(reference==buffer_snapshot());
        std::array<std::uint64_t,8> times{};
        for(auto& ns:times){prepare_buffer(bytes,vectors,offset,0);ns=execute(false);equal_cpu(reference_cpu);CHECK(reference==buffer_snapshot());}
        if(cases++)out<<',';out<<"{\"bytes\":"<<bytes<<",\"vector_clear_enabled\":"<<vectors<<",\"destination_offset\":"<<offset
            <<",\"ram_sha256\":\""<<wr::sha256(reference)<<"\",\"elapsed_ns\":[";
        for(unsigned n=0;n<times.size();++n){if(n)out<<',';out<<times[n];}out<<"],\"exact_cpu_fp_and_ram\":true}";
    }
    unsigned faults{};
    for(unsigned vectors:{0,1})for(unsigned mode:{1,2}){
        prepare_buffer(8192,vectors,3,0,mode);bool raised{};wr::FaultKind kind{};wr::U32 address{};
        try{execute(true);}catch(const wr::GuestFault& error){raised=true;kind=error.kind;address=error.address;}
        const auto reference_cpu=p.cpu;const auto reference=buffer_snapshot();
        prepare_buffer(8192,vectors,3,0,mode);bool now_raised{};
        try{execute(false);}catch(const wr::GuestFault& error){now_raised=true;CHECK(kind==error.kind && address==error.address);}
        CHECK(raised==now_raised);equal_cpu(reference_cpu);CHECK(reference==buffer_snapshot());faults+=raised;
    }
    out<<"],\"buffer_fault_snapshots\":"<<faults<<",\"native_weather_prefix_snapshots\":"<<prefix_cases<<",\"native_weather_control_snapshots\":"<<control_cases
        <<",\"native_weather_host_environment_snapshots\":"<<environment_cases<<",\"native_weather_decline_snapshots\":"<<decline_cases<<",\"source_function\":4225200,\"buffer_function\":4674608,\"game_launched\":false,\"screen_access\":false,\"device_opened\":false,\"whole_game_fps_measured\":false,\"passed\":true}\n";
    std::cout<<"PASS complete 16/64/128/400-particle function snapshots against checked static bodies, no frame loop.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
