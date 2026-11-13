// Finite synthetic font/sprite packets; no renderer, resource file or game UI.
#include "../recompiled/process.hpp"
#include "../recompiled/native_sprites.hpp"
#include "../recompiled/native_glyphs.hpp"
#include "../recompiled/glyph_budget.hpp"
#include "../recompiled/host_calls.hpp"
#include "../generated/shared-aot/bodies.hpp"
#include <fstream>
#include <iostream>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("glyph schedule check at ")+std::to_string(__LINE__)+": "+#x);}while(false)
namespace a=wa2::aot;using wr::U32;
static bool same(const wr::Cpu& x,const wr::Cpu& y){return x.r==y.r && x.eip==y.eip && x.flags==y.flags && x.defined_flags==y.defined_flags && x.fp.bytes==y.fp.bytes;}
int main(int argc,char** argv){try{
    CHECK(argc==2);a::Runtime runtime;runtime.load();auto& p=runtime.process();auto& m=p.memory;
    const auto data=p.allocate_bytes(65536),sprite=data+0x1000;
    m.store(0xb34004,1,32);m.store(0xb34000,data+0x2000,32);m.store(0xb3443c,data+0x2000,32);
    m.store(0xbc830c,1,32);m.store(0xbc8310,1,32);
    m.store(0xa823b1,28,8);m.store(0xa823b2,32,8);m.store(0xa823b3,40,8);
    m.store(sprite+0x48,0xffff,16);m.store(sprite+0x4a,0,16);m.store(sprite+0x4c,0xffff,16);
    m.store(sprite+0x15,4,8);m.store(sprite+0x80,0x3f800000,32);m.store(sprite+0x84,0x3f800000,32);
    for(unsigned font=0;font<4;++font){const auto resource=0xa93fb8+font*128;
        m.store(resource,1,8);m.store(resource+0x5c,font<2?3200:1920,16);m.store(resource+0x5e,font<2?3840:2304,16);
        m.store(resource+0x60,256,16);m.store(resource+0x62,256,16);m.store(resource+0x64,font<2?13:8,16);
        m.store(resource+0x66,font<2?15:9,16);m.store(resource+0x68,1,16);m.store(resource+0x70,data+0x4000,32);
    }
    for(unsigned n=0;n<256;++n)m.store(data+0x4000+n*4,data+0x8000+n*16,32);
    auto cpu=p.cpu;cpu.r[wr::ESP]-=0x4000;cpu.fp.put16(0,0x7f);cpu.eip=0x4462e0;
    const auto sp=cpu.r[wr::ESP];const U32 returning=0xf0001234;m.store(sp,returning,32);
    std::array<std::uint8_t,256> bytes;m.copy_out(sprite,bytes);m.copy_in(sp+4,bytes);
    const std::array<U32,9> args{'A',40,40,68,68,4,28,0xffffffffu,0};
    for(unsigned n=0;n<args.size();++n)m.store(sp+0x104+n*4,args[n],32);
    const auto incoming=cpu;unsigned submits{};
    a::SpriteService service(m,[](const a::SpriteCommand&,void* owner){++*static_cast<unsigned*>(owner);return true;},&submits);
    unsigned guards{};
    {a::GlyphCallScope exhausted(m,0);const auto before=a::glyph_budget_deferrals;
        std::array<std::uint8_t,0x128> stack;m.copy_out(sp,stack);
        CHECK(!a::native_draw_glyph(cpu,m) && same(cpu,incoming) && !submits);++guards;
        // Exercise the actual changed cached entry, not only the native helper.
        {auto scope=m.execution_scope();std::uint64_t unused{};a::f_4462e0<false>(cpu,m,scope,unused,0);}
        CHECK(same(cpu,incoming) && !submits && a::glyph_budget_deferrals==before+1);
        std::array<std::uint8_t,0x128> after;m.copy_out(sp,after);CHECK(after==stack);++guards;
        // A new outer entry supplies a new allowance and consumes the source
        // call exactly once, including its untouched return word/arguments.
        a::Program program(m);CHECK(program.invoke_unmetered(cpu));
        CHECK(cpu.eip==returning && cpu.r[wr::ESP]==sp+4 && submits==1 && !*a::GlyphCallScope::remaining(m));++guards;
        cpu=incoming;cpu.fp.put16(0,0x37f);const auto invalid=cpu;
        CHECK(!a::native_draw_glyph_or_yield(cpu,m) && same(cpu,invalid) && a::glyph_budget_deferrals==before+1);++guards;
    }
    cpu=incoming;
    {a::NativeCallScope generic(nullptr,nullptr);a::current_native_calls->remaining=0;
        CHECK(!a::native_draw_glyph_or_yield(cpu,m) && same(cpu,incoming));++guards;
        wr::Memory other;{a::GlyphCallScope mismatch(other,0);CHECK(!a::native_draw_glyph_or_yield(cpu,m) && same(cpu,incoming));++guards;}
    }
    {a::GlyphCallScope one(m,1);a::SpriteService decline(m,[](const a::SpriteCommand&,void*){return false;},nullptr);
        CHECK(!a::native_draw_glyph_or_yield(cpu,m) && same(cpu,incoming) && *a::GlyphCallScope::remaining(m)==1);++guards;
    }
    // 768 admitted glyph calls use three bounded groups, never the expensive
    // static chain merely because the previous group's allowance was spent.
    const auto before=a::glyph_budget_deferrals;const auto initial=submits;unsigned completed{},groups{};
    while(completed<768){a::GlyphCallScope bounded(m);++groups;
        while(completed<768){cpu=incoming;const auto count=submits;
            CHECK(a::native_draw_glyph_or_yield(cpu,m));
            if(cpu.eip==incoming.eip){CHECK(same(cpu,incoming) && submits==count && !*a::GlyphCallScope::remaining(m));break;}
            CHECK(cpu.eip==returning && cpu.r[wr::ESP]==sp+4 && submits==count+1);++completed;
        }
    }
    CHECK(groups==3 && submits-initial==768 && a::glyph_budget_deferrals==before+2);
    std::ofstream report(argv[1]);report<<"{\"glyphs\":768,\"bounded_groups\":3,\"deferrals\":2,\"guards\":"<<guards
        <<",\"maximum_glyphs_per_boundary\":256,\"source_entry_yields_checked\":true,\"next_entry_completes_exactly_once\":true,\"unchanged_cpu_stack_on_defer\":true,\"unsupported_inputs_still_decline\":true,\"no_legacy_draws\":true,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";
    CHECK(report);std::cout<<"PASS bounded glyph exhaustion yields and exactly-once native resume.\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
