#include "../recompiled/special/runtime.hpp"
#include "performance_profile.hpp"
#include "../platform/render/presentation_pacing.hpp"
#include <winrecomp/win32_state.hpp>
#include <fstream>
#include <iostream>

namespace s=wa2::special;namespace a=wa2::aot;using wr::U32;
unsigned checks{},predicates{},guards{};
#define CHECK(x) do{++checks;if(!(x))throw std::runtime_error("check failed: " #x);}while(false)
int main(int argc,char** argv){try{
    CHECK(argc==2);const auto& f=s::fast_forward_source;
    for(bool linear:{false,true}){
        wr::ProcessOptions options;options.prefer_linear_memory=linear;s::Runtime runtime(options);auto& p=runtime.process();p.load(s::initial_image());auto& m=p.memory;
        const auto reset=[&]{for(auto at:{f.inhibit,f.ui_blocked,f.skip_mode,f.skip_latch,f.input_held,f.line,f.script,f.allow_unread,f.script_skip,f.window_state,f.screen})m.store(at,0,32);m.store(f.current_state,3,32);m.store(f.previous_state,3,32);};
        for(U32 script:{0u,17u,1023u})for(U32 line:{0u,7u,4094u})for(U32 bits=0;bits<128;++bits)for(bool seen:{false,true}){
            reset();m.store(f.script,script,32);m.store(f.line,line,32);m.store(f.skip_mode,bits&1,32);m.store(f.skip_latch,(bits>>1)&1,32);m.store(f.input_held,(bits>>2)&1,8);m.store(f.allow_unread,(bits>>3)&1,32);m.store(f.script_skip,(bits>>4)&1,32);m.store(f.inhibit,(bits>>5)&1,32);m.store(f.ui_blocked,(bits>>6)&1,32);
            const auto byte=f.history+script*512+(line+1)/8;m.store(byte,seen?0x80u>>((line+1)%8):0,8);const auto latch=m.load(f.skip_latch,32);const auto before=p.cpu;
            const bool actual=a::source_fast_forward(m,f);CHECK(m.load(f.skip_latch,32)==latch && p.cpu.r==before.r && p.cpu.eip==before.eip);
            CHECK(actual==(p.callback(0x40a850,std::span<const U32>{},false)!=0));++predicates;
        }
        reset();m.store(f.skip_mode,1,32);m.store(f.input_held,1,8);CHECK(a::source_fast_forward(m,f));
        for(U32 screen:{5u,6u,7u,32u,0xffffffffu}){m.store(f.screen,screen,32);CHECK(!a::source_fast_forward(m,f));++guards;}m.store(f.screen,0,32);
        for(U32 at:{f.current_state,f.previous_state,f.window_state}){const auto old=m.load(at,32);m.store(at,at==f.window_state?1:4,32);CHECK(!a::source_fast_forward(m,f));m.store(at,old,32);++guards;}
        m.store(f.input_held,0,8);for(auto values:{std::pair{1024u,0u},std::pair{0u,4095u},std::pair{0u,0xffffffffu}}){m.store(f.script,values.first,32);m.store(f.line,values.second,32);CHECK(!a::source_fast_forward(m,f));++guards;}
        reset();m.store(f.skip_mode,1,32);m.store(f.input_held,1,8);const auto sleep=p.resolve("kernel32.dll","Sleep");
        const auto invoke=[&](U32 caller,U32 ms){const auto saved=p.cpu;wr::push(p.cpu,m,ms);wr::push(p.cpu,m,caller);p.cpu.eip=sleep;CHECK(p.dispatch_api());CHECK(p.cpu.eip==caller && p.cpu.r[wr::ESP]==saved.r[wr::ESP]);p.cpu=saved;};
        for(unsigned n=0;n<32;++n)invoke(f.idle_return,2);auto report=p.state().reports.at("scheduler")();CHECK(report.find("\"accelerated_idle_calls\":32")!=report.npos);
        invoke(0x434b2a,2);invoke(f.idle_return,1);m.store(f.screen,5,32);invoke(f.idle_return,2);report=p.state().reports.at("scheduler")();CHECK(report.find("\"accelerated_idle_calls\":32")!=report.npos);
        wa2::graphics::PresentationPacing pacing;pacing.reset(1);unsigned submits{},draws{};for(unsigned n=0;n<600;++n){++draws;const auto now=std::uint64_t(n)*1000000;const auto decision=pacing.plan(true,now);if(decision.submit){++submits;pacing.submitted(now);}}
        CHECK(draws==600 && submits>=60 && submits<=73);const auto restored=pacing.plan(false,700000000);CHECK(restored.submit && restored.swap_interval==1);
    }
    std::ofstream out(argv[1]);out<<"{\"passed\":true,\"checks\":"<<checks<<",\"source_predicate_comparisons\":"<<predicates<<",\"guards\":"<<guards<<",\"original_sleep_caller_checked\":true,\"settings_save_load_pacing_preserved\":true,\"all_draws_preserved\":true,\"auxiliary_pc_only\":true,\"game_launched\":false,\"screen_access\":false,\"android_gain_measured\":false}\n";CHECK(out);return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
