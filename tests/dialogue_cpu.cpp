// Bounded calls to the cached static glyph function, against owned RAM and
// a recording device. No game startup, resource file, window or GL context.
#include "process.hpp"
#include "work_samples.hpp"
#include "geometry_cache.hpp"
#include "native_sprites.hpp"
#include "native_glyphs.hpp"
#include "../engine/glyph_geometry_memo.hpp"
#include "../platform/render/submission_cache.hpp"
#include <winrecomp/win32_state.hpp>
#include <bit>
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
enum class Kind {unknown,d3d,device,texture,surface,vertexbuffer,indexbuffer,vertexshader,pixelshader,declaration,stateblock,swapchain,buffer,base_texture,resource};
#include "../port/d3d9_tables.inc"
#include "portrait_submission_snapshots.hpp"
#include "geometry_cache_snapshots.hpp"
namespace a=wa2::aot;
int main(int argc,char** argv){try{
    CHECK(argc==2);const auto cache_checks=geometry_cache_snapshots();a::Runtime runtime;auto& p=runtime.process();
    std::map<std::string,unsigned> calls;PortraitSubmission submission;
    std::vector<std::vector<std::uint8_t>> draws;
    unsigned fvf{},texture{},max_texture_slots{};std::array<unsigned,16> textures{};
    std::vector<unsigned> vtable;
    for(unsigned slot=0;slot<std::size(methods_device);++slot){const auto& method=methods_device[slot];
        p.register_api("dialogue-device",method.name,method.arguments,[&,name=std::string(method.name)](wr::Process& q){
            ++calls[name];const auto args=q.argument_snapshot();submission.method(name,args);
            if(name=="SetFVF")fvf=args[1];
            if(name=="SetTexture"){texture=args[2];textures.at(args[1])=args[2];}
            if(name=="DrawPrimitiveUP"){
                CHECK((args[1]==5 || args[1]==6) && args[2]<=32 && args[4]<=256);
                auto& bytes=draws.emplace_back((args[2]+2)*args[4]);q.memory.copy_out(args[3],bytes);
                submission.draw(args[1],args[2]+2,bytes,args[4]);
                unsigned used{};for(auto value:textures)used+=bool(value);max_texture_slots=std::max(max_texture_slots,used);
            }return 0u;
        });vtable.push_back(p.resolve("dialogue-device",method.name));
        if(wa2::graphics::direct_device_method(slot))p.certify_leaf_api("dialogue-device",method.name);
    }
    runtime.load();const auto data=p.allocate_bytes(65536),device=data,table=data+0x100,sprite=data+0x1000;
    p.memory.store(device,table,32);for(unsigned n=0;n<vtable.size();++n)p.memory.store(table+n*4,vtable[n],32);
    p.memory.store(0xb34438,device,32);p.memory.store(0xb34004,1,32);
    p.memory.store(0xb34000,data+0x2000,32);p.memory.store(0xb3443c,data+0x2000,32);
    p.memory.store(0xbc830c,1,32);p.memory.store(0xbc8310,1,32);
    p.memory.store(0xa823b1,28,8);p.memory.store(0xa823b2,32,8);p.memory.store(0xa823b3,40,8);
    p.memory.store(sprite+0x48,0xffff,16);p.memory.store(sprite+0x4a,0,16);p.memory.store(sprite+0x4c,0xffff,16);
    p.memory.store(sprite+0x15,4,8);
    const auto f=[&](unsigned at,float value){p.memory.store(at,std::bit_cast<unsigned>(value),32);};
    f(sprite+0x80,1);f(sprite+0x84,1);
    for(unsigned font=0;font<4;++font){const auto resource=0xa93fb8+font*128;
        p.memory.store(resource,1,8);p.memory.store(resource+0x5c,font<2?3200:1920,16);p.memory.store(resource+0x5e,font<2?3840:2304,16);
        p.memory.store(resource+0x60,256,16);p.memory.store(resource+0x62,256,16);
        p.memory.store(resource+0x64,font<2?13:8,16);p.memory.store(resource+0x66,font<2?15:9,16);
        p.memory.store(resource+0x68,1,16);p.memory.store(resource+0x70,data+0x4000,32);
    }
    for(unsigned n=0;n<256;++n)p.memory.store(data+0x4000+n*4,data+0x8000+n*16,32);
    p.callback(p.resolve("kernel32.dll","GetProcessHeap"),{});
    auto base=p.cpu;base.r[wr::ESP]-=0x8000;base.fp.put16(0,0x7f);
    std::array<std::uint8_t,256> template_sprite;p.memory.copy_out(sprite,template_sprite);
    a::Program program(p.memory);a::FunctionWorkSamples work(a::functions());
    unsigned variant{},stack_noise{},incoming_noise{};std::array<std::uint8_t,0x1a80> initial_stack;
    for(unsigned n=0;n<initial_stack.size();++n)initial_stack[n]=std::uint8_t(n*7+3);
    const auto run=[&](bool checked){
      for(unsigned n=0;n<initial_stack.size();++n)initial_stack[n]=std::uint8_t(n*7+stack_noise*29+3);
      p.memory.copy_in(base.r[wr::ESP]-0x1900,initial_stack);
      const float translation=float(variant)*0.25f;std::memcpy(template_sprite.data()+0x78,&translation,4);
      for(unsigned glyph=0;glyph<40;++glyph){p.cpu=base;const auto returning=0xf0001234u;
        for(unsigned n=0;n<8;++n)if(n!=wr::ESP)p.cpu.r[n]^=incoming_noise*(n*37+17);
        for(unsigned n=160;n<p.cpu.fp.bytes.size();++n)p.cpu.fp.bytes[n]^=std::uint8_t(incoming_noise+n*incoming_noise);
        p.memory.store(base.r[wr::ESP],returning,32);p.memory.copy_in(base.r[wr::ESP]+4,template_sprite);
        const auto x=40+glyph%14*28+(variant%2),y=740+glyph/14*42;
        const auto colour=variant%4==1?0xff000000u:variant%4==2?0x80ffffffu:0xffffffffu;
        const auto character=variant>=4?(glyph%2?0xcb90u:0xef8du):65+glyph%26;
        const auto size=variant>=8?20u:32u,margin=variant>=8?2u:4u,font=variant>=8?2u:0u;
        const std::array<unsigned,9> args{character,x,y,x+size,y+size,margin,size,colour,font};
        for(unsigned n=0;n<args.size();++n)p.memory.store(base.r[wr::ESP]+0x104+n*4,args[n],32);
        p.cpu.eip=0x4462e0;std::uint64_t budget=200000;unsigned boundaries{};
        while(p.cpu.eip!=returning){CHECK(++boundaries<50000);
            std::optional<a::LeafServiceScope> leaf;if(!checked)leaf.emplace([](wr::Cpu&,wr::Memory&,void* owner){return static_cast<wr::Process*>(owner)->dispatch_leaf_api();},&p);
            const auto pc=p.cpu.eip;bool known{};
            try{known=checked?program.invoke(p.cpu,budget):program.invoke_unmetered(p.cpu);}
            catch(const std::exception& error){std::cerr<<"dialogue source PC "<<std::hex<<pc<<" -> "<<p.cpu.eip<<" SP "<<p.cpu.r[wr::ESP]<<" base "<<base.r[wr::ESP]<<" output "<<p.memory.load(p.cpu.r[wr::ESP]+0x68,32)<<std::dec<<" draws "<<draws.size()<<'\n';throw;}
            leaf.reset();
            if(!known)CHECK(p.dispatch_api());
        }}
    };
    a::FunctionWorkScope sampled(&work);
    submission.active=true;const auto start=std::chrono::steady_clock::now();run(false);
    const auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
    CHECK(!draws.empty());submission.check(unsigned(draws.size()),1);submission.active=false;
    const auto original_draws=draws;const auto original_calls=calls;const auto original_cpu=p.cpu;
    std::array<std::array<std::uint64_t,2>,5> paired{};
    {a::FunctionWorkScope quiet(nullptr);
      for(unsigned pair=0;pair<paired.size();++pair)for(unsigned order=0;order<2;++order){const auto side=(pair+order)&1;a::native_geometry_cache_enabled=bool(side);
          for(unsigned n=0;n<3;++n){draws.clear();calls.clear();run(false);CHECK(draws==original_draws);}
          const auto begin=std::chrono::steady_clock::now();
          for(unsigned n=0;n<8;++n){draws.clear();calls.clear();run(false);CHECK(draws==original_draws);}
          paired[pair][side]=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-begin).count();
      }
    }
    std::array<std::uint64_t,2> timing{};
    for(unsigned side=0;side<2;++side){std::array<std::uint64_t,5> sorted;for(unsigned n=0;n<5;++n)sorted[n]=paired[n][side];std::sort(sorted.begin(),sorted.end());timing[side]=sorted[2];}
    CHECK(p.cpu.r==original_cpu.r && p.cpu.fp.bytes==original_cpu.fp.bytes && p.cpu.flags==original_cpu.flags && p.cpu.defined_flags==original_cpu.defined_flags);
    // Different glyphs, shadows, alpha/dissolve, fractional template motion,
    // and both font cell sizes must all invalidate/reuse exact transactions.
    unsigned exact_variants{};
    for(variant=0;variant<12;++variant){
        a::native_geometry_cache_enabled=false;draws.clear();calls.clear();run(false);
        const auto expected_draws=draws;const auto expected_calls=calls;const auto expected_cpu=p.cpu;
        std::array<std::uint8_t,0x1a80> expected_stack;p.memory.copy_out(base.r[wr::ESP]-0x1900,expected_stack);
        a::native_geometry_cache_enabled=true;
        for(unsigned repeat=0;repeat<3;++repeat){draws.clear();calls.clear();run(false);
            CHECK(draws==expected_draws && calls==expected_calls && p.cpu.r==expected_cpu.r && p.cpu.eip==expected_cpu.eip && p.cpu.flags==expected_cpu.flags && p.cpu.defined_flags==expected_cpu.defined_flags && p.cpu.fp.bytes==expected_cpu.fp.bytes);
            std::array<std::uint8_t,0x1a80> actual_stack;p.memory.copy_out(base.r[wr::ESP]-0x1900,actual_stack);CHECK(actual_stack==expected_stack);
        }++exact_variants;
    }
    variant=0;const auto before_noise=a::geometry_cache_counts();
    unsigned noise_checks{};
    for(unsigned n=1;n<=4;++n){stack_noise=n;incoming_noise=n;
        a::native_geometry_cache_enabled=false;draws.clear();calls.clear();run(false);
        const auto expected_draws=draws;const auto expected_calls=calls;const auto expected_cpu=p.cpu;
        std::array<std::uint8_t,0x1a80> expected_stack;p.memory.copy_out(base.r[wr::ESP]-0x1900,expected_stack);
        a::native_geometry_cache_enabled=true;draws.clear();calls.clear();run(false);
        CHECK(draws==expected_draws && calls==expected_calls && p.cpu.r==expected_cpu.r && p.cpu.fp.bytes==expected_cpu.fp.bytes && p.cpu.flags==expected_cpu.flags && p.cpu.defined_flags==expected_cpu.defined_flags);
        std::array<std::uint8_t,0x1a80> actual_stack;p.memory.copy_out(base.r[wr::ESP]-0x1900,actual_stack);CHECK(actual_stack==expected_stack);++noise_checks;
    }
    const auto after_noise=a::geometry_cache_counts();
    CHECK(after_noise.grid_hits-before_noise.grid_hits>=60);
    // Brand-new fast-forward lines must not pay for a capture that cannot
    // be reused. Each fresh position stays on the plain native calculation.
    const auto before_changing=a::geometry_cache_counts();
    for(unsigned n=0;n<8;++n){variant=100+n;stack_noise=20+n;incoming_noise=20+n;draws.clear();calls.clear();run(false);}
    const auto after_changing=a::geometry_cache_counts();
    CHECK(after_changing.grid_captures==before_changing.grid_captures && after_changing.sprite_captures==before_changing.sprite_captures);
    variant=0;std::array<std::array<std::uint64_t,2>,5> noisy_paired{};
    {a::FunctionWorkScope quiet(nullptr);
      for(unsigned pair=0;pair<5;++pair)for(unsigned order=0;order<2;++order){const auto side=(pair+order)&1;a::native_geometry_cache_enabled=bool(side);
        for(unsigned n=0;n<3;++n){stack_noise=40+pair*32+n;incoming_noise=stack_noise;draws.clear();calls.clear();run(false);CHECK(draws==original_draws);}
        const auto begin=std::chrono::steady_clock::now();
        for(unsigned n=0;n<8;++n){stack_noise=100+pair*32+n;incoming_noise=stack_noise;draws.clear();calls.clear();run(false);CHECK(draws==original_draws);}
        noisy_paired[pair][side]=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-begin).count();
      }
    }
    const auto memo=a::geometry_cache_counts();
    // Compare the complete native glyph entry with the previous release's
    // cached static chain. The command sink records the same raw tile bytes.
    std::array<std::array<std::uint64_t,2>,5> native_paired{};
    const auto sink=[](const a::SpriteCommand& command,void* owner){auto& output=*static_cast<std::vector<std::vector<std::uint8_t>>*>(owner);
        for(const auto& tile:command.tiles){const auto* bytes=reinterpret_cast<const std::uint8_t*>(tile.vertices.data());output.emplace_back(bytes,bytes+sizeof(tile.vertices));}return true;};
    unsigned native_variants{};std::uint64_t native_glyphs{},native_api_calls{};
    using GlyphMemo=wa2::text::GlyphGeometryMemo<a::SpriteTile>;
    GlyphMemo::Stats memo_before{},memo_after{};
    {a::FunctionWorkScope quiet(nullptr);a::native_geometry_cache_enabled=true;
      for(variant=0;variant<12;++variant){stack_noise=300+variant;incoming_noise=stack_noise;draws.clear();calls.clear();run(false);
        const auto expected=draws;const auto expected_cpu=p.cpu;
        draws.clear();calls.clear();const auto before=a::native_glyph_counts().accepted;
        {a::SpriteService native(p.memory,sink,&draws);run(false);}
        CHECK(a::native_glyph_counts().accepted-before==40);CHECK(draws==expected);
        CHECK(p.cpu.eip==expected_cpu.eip && p.cpu.r[wr::ESP]==expected_cpu.r[wr::ESP] && p.cpu.fp.get16(0)==expected_cpu.fp.get16(0) && !p.cpu.fp.bytes[4]);
        for(auto reg:{wr::EBX,wr::EBP,wr::ESI,wr::EDI})CHECK(p.cpu.r[reg]==expected_cpu.r[reg]);
        for(const auto& [name,count]:calls)native_api_calls+=count;
        native_glyphs+=40;++native_variants;
      }
      variant=0;
      memo_before=GlyphMemo::stats;
      for(unsigned pair=0;pair<5;++pair)for(unsigned order=0;order<2;++order){const auto side=(pair+order)&1;
        std::optional<a::SpriteService> native;if(side)native.emplace(p.memory,sink,&draws);
        const auto begin=std::chrono::steady_clock::now();
        for(unsigned n=0;n<8;++n){stack_noise=400+pair*16+n;incoming_noise=stack_noise;draws.clear();calls.clear();run(false);CHECK(draws==original_draws);}
        native_paired[pair][side]=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-begin).count();
      }
      memo_after=GlyphMemo::stats;
    }
    CHECK(!native_api_calls);
    // Changing lines and origins must not reuse an earlier glyph's geometry.
    // This exercises the actual complete native entry against static output,
    // independently of the memo's byte-key tests.
    unsigned changing_native_cases{};
    for(unsigned n=0;n<16;++n){variant=16+n;stack_noise=900+n;incoming_noise=stack_noise;
        draws.clear();calls.clear();run(false);const auto expected=draws;
        draws.clear();calls.clear();{a::SpriteService native(p.memory,sink,&draws);run(false);}
        CHECK(draws==expected && calls.empty());++changing_native_cases;
    }
    std::array<std::uint64_t,5> changing_native_ns{};unsigned line_serial=1000;
    for(unsigned pair=0;pair<5;++pair){a::SpriteService native(p.memory,sink,&draws);const auto begin=std::chrono::steady_clock::now();
        for(unsigned line=0;line<8;++line){variant=line_serial++;draws.clear();calls.clear();run(false);CHECK(calls.empty());}
        changing_native_ns[pair]=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-begin).count();}
    std::array<std::array<std::uint64_t,2>,5> memo_enabled_pairs{};
    variant=0;
    for(unsigned pair=0;pair<5;++pair)for(unsigned order=0;order<2;++order){const auto side=(pair+order)&1;GlyphMemo::enabled=bool(side);
        a::SpriteService native(p.memory,sink,&draws);for(unsigned n=0;n<3;++n){draws.clear();calls.clear();run(false);}
        const auto begin=std::chrono::steady_clock::now();for(unsigned n=0;n<8;++n){draws.clear();calls.clear();run(false);CHECK(draws==original_draws);}
        memo_enabled_pairs[pair][side]=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-begin).count();}
    GlyphMemo::enabled=true;
    std::ofstream out(argv[1]);out<<"{\"source_entry\":4481760,\"glyphs\":40,\"source_draws\":"<<original_draws.size()<<",\"fvf\":"<<fvf
        <<",\"bound_texture_units\":"<<max_texture_slots<<",\"elapsed_ns\":"<<elapsed<<",\"paired_glyph_sets\":8,\"uncached_ns\":"<<timing[0]<<",\"cached_ns\":"<<timing[1]
        <<",\"memo_grid_hits\":"<<memo.grid_hits<<",\"memo_grid_misses\":"<<memo.grid_misses<<",\"memo_sprite_hits\":"<<memo.sprite_hits<<",\"memo_sprite_misses\":"<<memo.sprite_misses
        <<",\"recorded_driver_packets\":"<<submission.packets<<",\"cache_key_checks\":"<<cache_checks<<",\"exact_source_variants\":"<<exact_variants
        <<",\"unrelated_input_variants\":"<<noise_checks<<",\"noise_grid_hits\":"<<after_noise.grid_hits-before_noise.grid_hits<<",\"noise_corner_hits\":"<<after_noise.sprite_hits-before_noise.sprite_hits
        <<",\"changing_lines_skip_capture\":true"
        <<",\"cached_outputs_bit_exact\":true,\"calls\":{";
    bool comma{};for(const auto& [name,count]:original_calls){if(comma)out<<',';comma=true;out<<wr::quote(name)<<':'<<count;}
    out<<"},\"paired_ns\":[";for(unsigned n=0;n<paired.size();++n){if(n)out<<',';out<<'['<<paired[n][0]<<','<<paired[n][1]<<']';}
    out<<"],\"noisy_paired_ns\":[";for(unsigned n=0;n<noisy_paired.size();++n){if(n)out<<',';out<<'['<<noisy_paired[n][0]<<','<<noisy_paired[n][1]<<']';}
    out<<"],\"native_glyph_chain\":{\"exact_source_variants\":"<<native_variants<<",\"accepted_glyphs\":"<<native_glyphs<<",\"legacy_api_calls\":"<<native_api_calls<<",\"paired_ns\":[";
    for(unsigned n=0;n<native_paired.size();++n){if(n)out<<',';out<<'['<<native_paired[n][0]<<','<<native_paired[n][1]<<']';}
    out<<"],\"memo_finds\":"<<memo_after.finds-memo_before.finds<<",\"memo_hits\":"<<memo_after.hits-memo_before.hits<<",\"memo_first_uses\":"<<memo_after.first_uses-memo_before.first_uses<<",\"memo_captures\":"<<memo_after.captures-memo_before.captures;
    out<<",\"changing_source_cases\":"<<changing_native_cases<<",\"changing_glyph_sets\":8,\"changing_native_ns\":[";
    for(unsigned n=0;n<5;++n){if(n)out<<',';out<<changing_native_ns[n];}
    out<<"],\"memo_enabled_pairs\":[";for(unsigned n=0;n<5;++n){if(n)out<<',';out<<'['<<memo_enabled_pairs[n][0]<<','<<memo_enabled_pairs[n][1]<<']';}
    out<<"],\"raw_vertex_bits_equal\":true,\"void_cdecl_abi_preserved\":true,\"raw_callee_volatile_x87_contract\":false}";
    out<<",\"work\":"<<work.report()<<",\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";
    std::cout<<"PASS cached dialogue recording: "<<draws.size()<<" source draws, "<<elapsed<<" ns.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
