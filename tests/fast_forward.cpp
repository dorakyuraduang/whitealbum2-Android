#include "process.hpp"
#include "fast_forward.hpp"
#include "../platform/render/presentation_pacing.hpp"
#include "../platform/compat/scheduler.hpp"
#include <winrecomp/win32_state.hpp>
#include <array>
#include <fstream>
#include <iostream>
#include <thread>

#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed at ")+std::to_string(__LINE__)+": "+#x);}while(false)
using wr::U32;
constexpr U32 IdleReturn=0x4337d8;
struct Counts {unsigned predicates{},guards{},idle{},polls{},workers{},pacing{};};

void source_and_scheduler(Counts& counts,bool linear){
    wr::ProcessOptions options;options.prefer_linear_memory=linear;
    wa2::aot::Runtime runtime(options);auto& p=runtime.process();auto& m=p.memory;
    U32 time=0xfffffff0;unsigned clock_calls{};
    p.replace_api("winmm.dll","timeGetTime",0,[&](wr::Process&){++clock_calls;return time;});
    runtime.load();
    CHECK(!p.gui() && !p.directdraw());
    const auto reset=[&]{
        for(auto at:{0x4be03cu,0x4c2240u,0xa39788u,0x4be018u,0x4be00cu,0xa391e4u,0xa391e8u,0x4be048u,0xa391f4u,0x4c2234u,0xa39208u})m.store(at,0,32);
        m.store(0xa3a36c,3,32);m.store(0xa3a380,3,32);
    };
    // Compare the read-only policy with the actual unchanged static predicate
    // on own-memory seen/unseen text, both inputs, cancellation and overrides.
    for(U32 script:{0u,1u,1023u})for(U32 line:{0u,6u,7u,8u,4094u})
    for(U32 flags=0;flags<128;++flags){
        reset();m.store(0xa391e4,script,32);m.store(0xa391e8,line,32);
        m.store(0xa39788,flags&1,32);m.store(0x4be018,(flags>>1)&1,32);
        m.store(0x4be00c,(flags>>2)&1,8);m.store(0x4be048,(flags>>3)&1,32);
        m.store(0xa391f4,(flags>>4)&1,32);m.store(0x4be03c,(flags>>5)&1,32);
        m.store(0x4c2240,(flags>>6)&1,32);
        for(bool seen:{false,true}){
            const auto address=0x567360+script*512+(line+1)/8;
            m.store(address,seen?0x80u>>((line+1)%8):0,8);
            m.store(0x4be018,(flags>>1)&1,32);const auto latch=m.load(0x4be018,32);
            const auto before=p.cpu;
            const bool predicted=wa2::aot::source_fast_forward(m);
            CHECK(m.load(0x4be018,32)==latch && p.cpu.r==before.r && p.cpu.eip==before.eip);
            CHECK(predicted==(p.callback(0x40ad10,std::span<const U32>{},false)!=0));
            ++counts.predicates;
        }
    }
    reset();m.store(0xa39788,1,32);m.store(0x4be00c,1,8);
    CHECK(wa2::aot::source_fast_forward(m));
    for(U32 screen:{6u,7u}){m.store(0xa39208,screen,32);CHECK(!wa2::aot::source_fast_forward(m));++counts.guards;}m.store(0xa39208,0,32);
    for(U32 at:{0xa3a36cu,0xa3a380u,0x4c2234u}){
        const auto before=m.load(at,32);m.store(at,at==0x4c2234?2:4,32);
        CHECK(!wa2::aot::source_fast_forward(m));m.store(at,before,32);++counts.guards;
    }
    m.store(0x4be00c,0,8);m.store(0x4be048,1,32);
    for(auto [script,line]:std::array<std::pair<U32,U32>,5>{{{0,0xffffffffu},{1024,0},{0,4095},{0,4096},{0xffffffffu,0}}}){
        m.store(0xa391e4,script,32);m.store(0xa391e8,line,32);
        CHECK(!wa2::aot::source_fast_forward(m));++counts.guards;
    }
    reset();m.store(0xa39788,1,32);m.store(0x4be00c,1,8);
    const auto sleep=p.resolve("kernel32.dll","Sleep");
    p.state().poll.push_back([&]{++counts.polls;});
    const auto invoke=[&](U32 ms,U32 caller){
        const auto saved=p.cpu;wr::push(p.cpu,m,ms);wr::push(p.cpu,m,caller);p.cpu.eip=sleep;
        CHECK(p.dispatch_api());CHECK(p.cpu.eip==caller && p.cpu.r[wr::ESP]==saved.r[wr::ESP]);p.cpu=saved;
    };
    const auto report=[&]{return p.state().reports.at("scheduler")();};
    for(unsigned n=0;n<32;++n){invoke(2,IdleReturn);++counts.idle;}
    CHECK(report().find("\"accelerated_idle_calls\":32")!=std::string::npos);
    CHECK(counts.polls>=32 && clock_calls==0 && time==0xfffffff0);
    // Wrong callsites, worker IDs, changed story/input state and host-internal
    // sleeps must not enter the idle shortcut. Check counts, not wall-time FPS.
    invoke(0,IdleReturn);invoke(1,IdleReturn);invoke(2,0x43387a);invoke(2,0x43388d);
    p.state().current_thread_id=2;invoke(2,IdleReturn);p.state().current_thread_id=1;
    m.store(0xa39788,0,32);invoke(2,IdleReturn);m.store(0xa39788,1,32);
    m.store(0xa3a36c,4,32);invoke(2,IdleReturn);m.store(0xa3a36c,3,32);
    for(U32 screen:{6u,7u}){m.store(0xa39208,screen,32);invoke(2,IdleReturn);}m.store(0xa39208,0,32);
    const auto saved=p.cpu;m.store(p.cpu.r[wr::ESP],IdleReturn,32);p.state().sleep_thread(2);p.cpu=saved;
    CHECK(report().find("\"accelerated_idle_calls\":32")!=std::string::npos);
    CHECK(p.callback(p.resolve("winmm.dll","timeGetTime"),{})==time && clock_calls==1);
    const auto event=p.callback(p.resolve("kernel32.dll","CreateEventA"),std::array<U32,4>{0,0,0,0});
    CHECK(event && p.callback(p.resolve("kernel32.dll","WaitForSingleObject"),std::array<U32,2>{event,0})==258);
    CHECK(p.callback(p.resolve("kernel32.dll","CloseHandle"),std::array<U32,1>{event})==1);
    CHECK(!p.gui() && !p.directdraw() && !p.exited());
}

void worker_sleep(Counts& counts){
    constexpr U32 Start=0x501000;
    U32 sleep{};unsigned policy_calls{};
    // Explicit synthetic compiled worker, not a source game or decoder.
    wr::Process p(wr::FunctionExecutor{[&](wr::Cpu& cpu,wr::Memory& memory){
        if(cpu.eip==Start){wr::push(cpu,memory,2);wr::push(cpu,memory,IdleReturn);cpu.eip=sleep;return true;}
        if(cpu.eip==IdleReturn){cpu.r[wr::EAX]=17;cpu.eip=wr::pop(cpu,memory);cpu.r[wr::ESP]+=4;return true;}
        return false;
    }});
    wa2::platform::compat::install_scheduler(p,[&](auto,auto){++policy_calls;return true;});
    wr::Image image;image.base=0x400000;image.size=0x102000;image.headers_size=4096;image.entry=Start;image.bytes.resize(0x102000,0x90);
    image.sections={{".text",4096,0x101000,4096,0x101000,0x60000020}};p.load(image);
    sleep=p.resolve("kernel32.dll","Sleep");
    const auto thread=p.callback(p.resolve("kernel32.dll","CreateThread"),std::array<U32,6>{0,0,Start,0,0,0});CHECK(thread);
    const auto poll=[&]{for(auto& callback:p.state().poll)callback();};poll();
    CHECK(policy_calls==0);
    std::this_thread::sleep_for(std::chrono::milliseconds(3));poll();
    const auto result=p.allocate_bytes(4);
    CHECK(p.callback(p.resolve("kernel32.dll","GetExitCodeThread"),std::array<U32,2>{thread,result})==1 && p.memory.load(result,32)==17);
    CHECK(policy_calls==0);++counts.workers;
}

void presentation(Counts& counts){
    using wa2::graphics::PresentationPacing;
    for(int source:{0,1}){
        PresentationPacing pacing;pacing.reset(source);
        unsigned swaps{},changes{};int driver=source;
        for(std::uint64_t n=0;n<1000;++n){
            const auto at=n*1000000;const auto decision=pacing.plan(true,at);
            if(decision.swap_interval){driver=*decision.swap_interval;++changes;}
            if(decision.submit){++swaps;pacing.submitted(at);}
        }
        CHECK(swaps>100 && swaps<=120 && driver==0 && changes==unsigned(source!=0));
        const auto leave=pacing.plan(false,999000001);
        CHECK(leave.submit && leave.swap_interval==(source?std::optional<int>(1):std::nullopt));
        CHECK(pacing.plan(false,999000001).submit);counts.pacing+=1002;
        // Entry and restoration are immediate, including the same timestamp.
        const auto enter=pacing.plan(true,999000001);CHECK(enter.submit);pacing.submitted(999000001);
        CHECK(!pacing.plan(true,999000002).submit);
        CHECK(pacing.plan(true,999000002,true).submit); // forced movie restoration
        CHECK(pacing.plan(false,999000002).submit); // movie / menu
        CHECK(pacing.plan(true,999000002).submit);
        pacing.submitted(1000000000);
        CHECK(!pacing.plan(true,1000000000+PresentationPacing::minimum_interval_ns-1).submit);
        CHECK(pacing.plan(true,1000000000+PresentationPacing::minimum_interval_ns).submit);
        CHECK(pacing.plan(true,1).submit);counts.pacing+=8;
        // A rejected driver request is not retried every frame, but leaving
        // acceleration still sends the original synchronization request.
        pacing.reset(source);unsigned attempts{};std::uint64_t now{};
        for(unsigned n=0;n<100;++n){const auto d=pacing.plan(true,now);if(d.swap_interval)++attempts;if(d.submit){now+=16666667;pacing.submitted(now);}now+=1000;}
        CHECK(attempts==unsigned(source!=0));CHECK(pacing.plan(false,now).submit);++counts.pacing;
    }
    wr::Memory unmapped;CHECK(!wa2::aot::source_fast_forward(unmapped));++counts.guards;
}
int main(int argc,char** argv){try{
    CHECK(argc==2);Counts counts;for(bool linear:{false,true})source_and_scheduler(counts,linear);
    worker_sleep(counts);presentation(counts);
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.fast-forward-memory.v1\",\"static_predicate_comparisons\":"<<counts.predicates
        <<",\"decline_cases\":"<<counts.guards<<",\"accelerated_idle_calls\":"<<counts.idle<<",\"poll_callbacks\":"<<counts.polls
        <<",\"worker_sleep_cases\":"<<counts.workers<<",\"presentation_decisions\":"<<counts.pacing
        <<",\"unchanged_static_predicate\":true,\"unread_policy_preserved\":true,\"worker_and_object_waits_preserved\":true,\"game_clock_unscaled\":true"
          ",\"source_sync_restored\":true,\"presentation_only_coalescing\":true,\"source_executable_opened\":false,\"game_launched\":false,\"screen_access\":false,\"graphics_device_created\":false,\"audio_device_opened\":false,\"whole_game_speed_measured\":false,\"passed\":true}\n";
    CHECK(out.good());std::cout<<"PASS fast-forward policy: "<<counts.predicates<<" static predicate comparisons, source-idle/worker/wait checks and bounded presentation scheduling.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
