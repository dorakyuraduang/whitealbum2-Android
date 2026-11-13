// Finite text-helper calls with constructed text, font metadata and a recording
// device. No game startup, original resource, window, GL context or screen.
#include "../recompiled/process.hpp"
#include "../recompiled/native_sprites.hpp"
#include "../recompiled/native_glyphs.hpp"
#include "../recompiled/glyph_budget.hpp"
#include "../recompiled/glyph_resources.hpp"
#include "../recompiled/host_calls.hpp"
#include "../engine/glyph_index.hpp"
#include "../platform/render/submission_cache.hpp"
#include <winrecomp/win32_state.hpp>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("novel check at ")+std::to_string(__LINE__)+": "+#x);}while(false)
using wr::U32;
namespace a=wa2::aot;
enum class Kind {unknown,d3d,device,texture,surface,vertexbuffer,indexbuffer,vertexshader,pixelshader,declaration,stateblock,swapchain,buffer,base_texture,resource};
#include "../port/d3d9_tables.inc"

struct Tile {
    U32 texture;
    std::array<U32,32> vertices;
    bool operator==(const Tile&)const=default;
};
struct Recording {
    std::map<std::string,unsigned> calls;
    std::vector<Tile> tiles;
    U32 texture{};
    static bool submit(const a::SpriteCommand& command,void* owner){
        auto& self=*static_cast<Recording*>(owner);
        for(const auto& input:command.tiles){auto& tile=self.tiles.emplace_back();tile.texture=input.texture;
            std::memcpy(tile.vertices.data(),input.vertices.data(),sizeof(tile.vertices));}
        return true;
    }
    void reset(){calls.clear();tiles.clear();texture=0;}
};
int main(int argc,char** argv){try{
    CHECK(argc==2);a::Runtime runtime;auto& p=runtime.process();Recording recorded;
    p.replace_api("user32.dll","MessageBoxA",4,[](wr::Process&)->U32{throw std::runtime_error("Constructed text exceeds source text-helper contract");});
    std::vector<U32> vtable;
    for(unsigned slot=0;slot<std::size(methods_device);++slot){const auto& method=methods_device[slot];
        p.register_api("novel-recording-device",method.name,method.arguments,[&,name=std::string(method.name)](wr::Process& q){
            ++recorded.calls[name];const auto args=q.argument_snapshot();
            if(name=="SetTexture" && !args[1])recorded.texture=args[2];
            if(name=="DrawPrimitiveUP"){
                CHECK(args[1]==5 && args[2]==2 && args[4]==sizeof(a::SpriteVertex));
                auto& tile=recorded.tiles.emplace_back();tile.texture=recorded.texture;
                q.memory.copy_out(args[3],{reinterpret_cast<std::uint8_t*>(tile.vertices.data()),sizeof(tile.vertices)});
            }return 0u;
        });vtable.push_back(p.resolve("novel-recording-device",method.name));
        if(wa2::graphics::direct_device_method(slot))p.certify_leaf_api("novel-recording-device",method.name);
    }
    runtime.load();const auto data=p.allocate_bytes(65536),device=data,table=data+0x100,output=data+0x1000;
    p.memory.store(device,table,32);for(unsigned n=0;n<vtable.size();++n)p.memory.store(table+n*4,vtable[n],32);
    p.memory.store(0xb34438,device,32);p.memory.store(0xb34004,1,32);
    p.memory.store(0xb34000,data+0x2000,32);p.memory.store(0xb3443c,data+0x2000,32);
    p.memory.store(0xbc830c,1,32);p.memory.store(0xbc8310,1,32);
    p.memory.store(0xa823b1,28,8);p.memory.store(0xa823b2,32,8);p.memory.store(0xa823b3,40,8);
    p.memory.store(output+0x48,0xffff,16);p.memory.store(output+0x4a,0,16);p.memory.store(output+0x4c,0xffff,16);
    p.memory.store(output+0x15,4,8);p.memory.store(output+0x80,0x3f800000,32);p.memory.store(output+0x84,0x3f800000,32);
    for(unsigned font=0;font<4;++font){const auto resource=0xa93fb8+font*128;
        p.memory.store(resource,1,8);p.memory.store(resource+0x5c,font<2?3200:1920,16);p.memory.store(resource+0x5e,font<2?3840:2304,16);
        p.memory.store(resource+0x60,256,16);p.memory.store(resource+0x62,256,16);p.memory.store(resource+0x64,font<2?13:8,16);
        p.memory.store(resource+0x66,font<2?15:9,16);p.memory.store(resource+0x68,1,16);p.memory.store(resource+0x70,data+0x4000,32);
    }
    for(unsigned n=0;n<256;++n)p.memory.store(data+0x4000+n*4,data+0x8000+n*16,32);
    p.callback(p.resolve("kernel32.dll","GetProcessHeap"),{});
    const auto initial_cpu=p.cpu;
    std::array<std::uint8_t,0x2000> initial_output;p.memory.copy_out(output,initial_output);
    std::array<std::uint8_t,256> sprite;p.memory.copy_out(output,sprite);
    std::ofstream report(argv[1]);report<<"{\"pages\":[";bool comma{};unsigned exact_pages{},unchanged_page_checks{};
    for(unsigned variant=0;variant<4;++variant){
        const bool two_byte=variant>=2;const unsigned count=variant&1?(two_byte?80:200):40;
        std::string text;for(unsigned n=0;n<count;++n){
            if(two_byte)text+=n&1?"\x90\xcb":"\x8d\xef";else text+=char('A'+n%26);
        }const auto raw=p.put_string(text);
        std::array<U32,26> args{output,0,14,28,4,0,14,0,0,0,28,raw,0xffffffffu,0,255,255,255,255,0,0,0,0,0,0,0,1};
        args[24]=data+0x3000;
        const auto run=[&](bool native){
            recorded.reset();p.cpu=initial_cpu;p.cpu.fp.put16(0,0x7f);p.memory.copy_in(output,initial_output);
            const auto start=std::chrono::steady_clock::now();
            {std::optional<a::SpriteService> sink;if(native)sink.emplace(p.memory,Recording::submit,&recorded);p.callback(0x446500,args,false);}
            return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
        };
        const auto static_ns=run(false);const auto expected=recorded.tiles;CHECK(expected.size()>=count);
        const auto expected_cpu=p.cpu;std::array<std::uint8_t,0x2000> expected_output;p.memory.copy_out(output,expected_output);
        const auto before=a::native_glyph_counts();const auto resources_before=a::glyph_resource_counts;
        const auto native_ns=run(true);const auto after=a::native_glyph_counts();const auto resources_after=a::glyph_resource_counts;
        const auto single_reads=resources_after.single_texture_handle_reads-resources_before.single_texture_handle_reads;
        const auto copied_words=resources_after.full_texture_handle_words-resources_before.full_texture_handle_words;
        CHECK(single_reads>0 && single_reads+copied_words<count*195);
        const auto check_result=[&]{
            CHECK(recorded.tiles==expected && recorded.calls.empty());
            CHECK(p.cpu.eip==expected_cpu.eip && p.cpu.r[wr::ESP]==expected_cpu.r[wr::ESP]);
            CHECK(p.cpu.fp.get16(0)==expected_cpu.fp.get16(0) && !p.cpu.fp.bytes[4]);
            for(auto reg:{wr::EBX,wr::EBP,wr::ESI,wr::EDI})CHECK(p.cpu.r[reg]==expected_cpu.r[reg]);
            std::array<std::uint8_t,0x2000> actual;p.memory.copy_out(output,actual);CHECK(actual==expected_output);
        };
        check_result();CHECK(after.accepted-before.accepted==count && after.declined==before.declined);++exact_pages;
        // Resubmit the already-visible page without input/resource changes.
        for(unsigned repeat=0;repeat<2;++repeat){run(true);check_result();++unchanged_page_checks;}
        if(comma)report<<',';comma=true;
        report<<"{\"characters\":"<<count<<",\"two_byte\":"<<(two_byte?"true":"false")<<",\"native_glyphs\":"<<after.accepted-before.accepted
            <<",\"native_tiles\":"<<expected.size()<<",\"legacy_draws\":0,\"legacy_state_calls\":0,\"single_handle_reads\":"<<single_reads<<",\"copied_handle_words\":"<<copied_words
            <<",\"previous_minimum_handle_words\":"<<count*195<<",\"static_chain_ns\":"<<static_ns<<",\"native_ns\":"<<native_ns<<'}';
        std::cout<<"Constructed page: "<<count<<" chars, exact "<<expected.size()<<" tiles, no legacy calls, static/native ns "<<static_ns<<'/'<<native_ns<<'\n';
    }
    // The new allowance is finite and independent of the generic call budget.
    unsigned guard_cases{};
    {wr::Memory other;CHECK(!a::GlyphCallScope::remaining(p.memory));
        a::GlyphCallScope outer(p.memory,999);auto* remaining=a::GlyphCallScope::remaining(p.memory);
        CHECK(remaining && *remaining==256 && !a::GlyphCallScope::remaining(other));++guard_cases;
        --*remaining;
        {a::GlyphCallScope nested(other,2);CHECK(!a::GlyphCallScope::remaining(p.memory));CHECK(*a::GlyphCallScope::remaining(other)==2);++guard_cases;}
        CHECK(a::GlyphCallScope::remaining(p.memory)==remaining && *remaining==255);++guard_cases;
        {a::GlyphCallScope nested(p.memory,1);CHECK(*a::GlyphCallScope::remaining(p.memory)==1);++guard_cases;}
        CHECK(a::GlyphCallScope::remaining(p.memory)==remaining && *remaining==255);++guard_cases;
    }
    CHECK(!a::GlyphCallScope::remaining(p.memory));++guard_cases;
    auto cpu=initial_cpu;cpu.r[wr::ESP]-=0x4000;cpu.fp.put16(0,0x7f);cpu.eip=0x4462e0;
    const auto sp=cpu.r[wr::ESP];p.memory.store(sp,0xf0001234u,32);p.memory.copy_in(sp+4,sprite);
    const std::array<U32,9> glyph_args{'A',40,40,68,68,4,28,0xffffffffu,0};
    for(unsigned n=0;n<glyph_args.size();++n)p.memory.store(sp+0x104+n*4,glyph_args[n],32);
    const auto check_unchanged=[&](const wr::Cpu& before,const std::array<std::uint8_t,0x128>& stack,std::size_t tiles){
        CHECK(cpu.eip==before.eip && cpu.r==before.r && cpu.fp.bytes==before.fp.bytes && cpu.flags==before.flags && cpu.defined_flags==before.defined_flags);
        std::array<std::uint8_t,0x128> after;p.memory.copy_out(sp,after);CHECK(after==stack && recorded.tiles.size()==tiles);++guard_cases;
    };
    {a::SpriteService sink(p.memory,Recording::submit,&recorded);
        a::NativeCallScope generic(nullptr,nullptr);CHECK(a::current_native_calls->remaining==32);
        a::current_native_calls->remaining=0;
        {a::GlyphCallScope glyphs(p.memory);
            for(unsigned n=0;n<256;++n){cpu.eip=0x4462e0;cpu.r[wr::ESP]=sp;CHECK(a::native_draw_glyph(cpu,p.memory));CHECK(!a::current_native_calls->remaining);}
            CHECK(!*a::GlyphCallScope::remaining(p.memory));++guard_cases;
            cpu.eip=0x4462e0;cpu.r[wr::ESP]=sp;const auto before=cpu;std::array<std::uint8_t,0x128> stack;p.memory.copy_out(sp,stack);
            const auto tiles=recorded.tiles.size(),exhaustions=a::glyph_budget_counts.exhaustions;
            CHECK(!a::native_draw_glyph(cpu,p.memory));CHECK(a::glyph_budget_counts.exhaustions==exhaustions+1);check_unchanged(before,stack,tiles);
        }
        // Calls outside a Program glyph scope keep the previous generic limit.
        const auto before=cpu;std::array<std::uint8_t,0x128> stack;p.memory.copy_out(sp,stack);const auto tiles=recorded.tiles.size();
        CHECK(!a::native_draw_glyph(cpu,p.memory));check_unchanged(before,stack,tiles);
        a::current_native_calls->remaining=1;CHECK(a::native_draw_glyph(cpu,p.memory));CHECK(!a::current_native_calls->remaining);++guard_cases;
        {a::GlyphCallScope glyphs(p.memory,1);
            unsigned declines{};a::SpriteService decline(p.memory,[](const a::SpriteCommand&,void* owner){++*static_cast<unsigned*>(owner);return false;},&declines);
            cpu.eip=0x4462e0;cpu.r[wr::ESP]=sp;const auto before=cpu;std::array<std::uint8_t,0x128> stack;p.memory.copy_out(sp,stack);const auto tiles=recorded.tiles.size();
            CHECK(!a::native_draw_glyph(cpu,p.memory) && declines==1 && *a::GlyphCallScope::remaining(p.memory)==1);
            check_unchanged(before,stack,tiles);
        }
    }
    unsigned handle_guards{};
    {a::SpriteService sink(p.memory,Recording::submit,&recorded);a::GlyphCallScope glyphs(p.memory);
        wa2::text::GlyphIndex index;std::array<std::uint8_t,256> table;p.memory.copy_out(0x4a14b8,table);
        const auto resolved=index.lookup(table,'A');CHECK(resolved);
        const auto tile_index=((*resolved/80*40+4)/256)*13+(*resolved%80*40+4)/256;
        const auto slot=data+0x4000+tile_index*4,original=p.memory.load(slot,32);
        for(auto value:{original+16,0u,original}){
            p.memory.store(slot,value,32);cpu.eip=0x4462e0;cpu.r[wr::ESP]=sp;recorded.reset();
            CHECK(a::native_draw_glyph(cpu,p.memory));
            if(value)CHECK(recorded.tiles.size()==1 && recorded.tiles[0].texture==value);else CHECK(recorded.tiles.empty());
            ++handle_guards;
        }
        // The selected word is readable but an UNUSED suffix is protected.
        // Retain the former full-table guard before admitting a lazy read.
        const auto font=0xa93fb8u,old_pointer=p.memory.load(font+0x70,32),pointer=data+0x5000-4-tile_index*4;
        p.memory.store(pointer+tile_index*4,original,32);p.memory.store(font+0x70,pointer,32);
        p.memory.protect(data+0x5000,4096,0);
        cpu.eip=0x4462e0;cpu.r[wr::ESP]=sp;auto before=cpu;std::array<std::uint8_t,0x128> stack;p.memory.copy_out(sp,stack);auto tiles=recorded.tiles.size();
        CHECK(!a::native_draw_glyph(cpu,p.memory));check_unchanged(before,stack,tiles);++handle_guards;
        p.memory.protect(data+0x5000,4096,wr::Memory::Read|wr::Memory::Write);
        p.memory.store(font+0x70,0xfffffffcu,32);before=cpu;
        CHECK(!a::native_draw_glyph(cpu,p.memory));check_unchanged(before,stack,tiles);++handle_guards;
        p.memory.store(font+0x70,old_pointer,32);
    }
    report<<"],\"exact_full_text_cases\":"<<exact_pages<<",\"unchanged_page_checks\":"<<unchanged_page_checks<<",\"guard_cases\":"<<guard_cases<<",\"handle_guard_cases\":"<<handle_guards
        <<",\"maximum_glyphs_per_boundary\":256,\"generic_call_limit_unchanged\":true,\"nested_memory_scopes_checked\":true,\"exhaustion_fallback_preserves_state\":true"
        <<",\"exact_texture_order_vertices_output_and_abi\":true,\"complete_handle_range_still_checked\":true,\"handle_changes_visible_immediately\":true,\"game_launched\":false,\"screen_access\":false,\"source_executable_opened\":false,\"passed\":true}\n";
    CHECK(report);std::cout<<"Finite text helper and budget checks passed.\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
