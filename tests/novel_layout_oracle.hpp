#pragma once
#include "../recompiled/text_layout_replay_control.hpp"

namespace novel_layout_test {

struct CpuImage {
    wr::Cpu value;
    bool operator==(const CpuImage& other) const {
        const auto& a = value;
        const auto& b = other.value;
        return a.r == b.r && a.eip == b.eip && a.flags == b.flags && a.defined_flags == b.defined_flags &&
            a.fs_base == b.fs_base && a.gs_base == b.gs_base && a.fs_valid == b.fs_valid &&
            a.gs_valid == b.gs_valid && a.selectors == b.selectors && a.fp.bytes == b.fp.bytes;
    }
};

struct Command {
    std::array<U32,12> fields;
    std::vector<Tile> tiles;
    bool operator==(const Command&) const = default;
};

using Stack = std::array<std::uint8_t,4096>;
struct Boundary {
    CpuImage cpu;
    Stack stack;
    bool operator==(const Boundary&) const = default;
};
struct Trace {
    std::vector<Command> commands;
    std::vector<Boundary> boundaries;
    std::optional<std::pair<wr::FaultKind,U32>> fault;
    std::array<std::uint8_t,0x3000> output_and_cursor;
    std::array<std::uint8_t,0x100> globals;
    bool operator==(const Trace&) const = default;
};
inline void compare(const Trace& expected,const Trace& actual,const char* name) {
    if(expected == actual) return;
    std::ostringstream message;
    message << "Layout oracle " << name;
    for(std::size_t i=0;i<std::min(expected.boundaries.size(),actual.boundaries.size());++i) {
        const auto& a = expected.boundaries[i];
        const auto& b = actual.boundaries[i];
        if(a == b) continue;
        message << " boundary " << i << " PC " << std::hex << a.cpu.value.eip << '/' << b.cpu.value.eip;
        const auto diff = std::mismatch(a.stack.begin(),a.stack.end(),b.stack.begin());
        if(diff.first != a.stack.end()) message << " stack offset " << diff.first-a.stack.begin();
        if(a.cpu.value.r != b.cpu.value.r) message << " integer registers";
        if(a.cpu.value.fp.bytes != b.cpu.value.fp.bytes) message << " FP registers";
        throw std::runtime_error(message.str());
    }
    message << " boundaries " << expected.boundaries.size() << '/' << actual.boundaries.size()
            << ", commands " << expected.commands.size() << '/' << actual.commands.size();
    throw std::runtime_error(message.str());
}
struct Observer {
    wr::Process& p;
    U32 stack_base;
    Trace& trace;
    bool force_yield;
    void checkpoint() {
        auto& point = trace.boundaries.emplace_back();
        point.cpu.value = p.cpu;
        p.memory.copy_out(stack_base,point.stack);
    }
    static bool submit(const a::SpriteCommand& value,void* owner) {
        auto& self = *static_cast<Observer*>(owner);
        self.checkpoint();
        Command command{{value.device,value.target,U32(value.target_kind),value.blend_enabled,value.blend_op,
            value.source_blend,value.destination_blend,value.minimum,value.magnification,value.mipmap,
            value.alpha_argument,U32(value.glyph)},{}};
        for(const auto& tile : value.tiles) {
            auto& copy = command.tiles.emplace_back();
            copy.texture = tile.texture;
            std::memcpy(copy.vertices.data(),tile.vertices.data(),sizeof(copy.vertices));
        }
        self.trace.commands.push_back(std::move(command));
        if(self.force_yield && self.trace.commands.size() == 12) {
            auto* remaining = a::GlyphCallScope::remaining(self.p.memory);
            CHECK(remaining && *remaining);
            *remaining = 1;
        }
        return true;
    }
};

// Use the retained function with the optimization disabled as the oracle.
// Drive its actual dispatch boundaries so the callback wrapper cannot restore
// the source registers before comparison. Snapshot the complete scratch stack
// at each yield and each typed draw, including the by-value sprite arguments.
inline unsigned verify(wr::Process& p,const wr::Cpu& initial,U32 output,std::array<U32,26> args) {
    const auto saved_cpu = p.cpu;
    const bool saved_enabled = a::text_layout_replay_enabled;
    const U32 stack_base = initial.r[wr::ESP] - 8192;
    std::array<std::uint8_t,8192> stack;
    p.memory.copy_out(stack_base,stack);
    std::array<std::uint8_t,0x3000> initial_output;
    std::array<std::uint8_t,0x100> initial_globals;
    p.memory.copy_out(output,initial_output);
    p.memory.copy_out(0xb34500,initial_globals);
    const U32 text = args[11];
    const U32 text_word = p.memory.load(text + 4,16);
    const U32 font_width = p.memory.load(0xa823b1,8);
    std::array<std::uint8_t,52> original_font,alternate_font;
    p.memory.copy_out(0xa823b0,original_font);
    p.memory.copy_out(0xa823b0+52,alternate_font);
    const auto font_permissions = p.memory.permissions(0xa82000);
    const auto restore = [&] {
        if(p.memory.permissions(0xa82000) != font_permissions)
            p.memory.protect(0xa82000,4096,font_permissions);
        p.memory.store(0xa823b1,font_width,8);
        p.memory.copy_in(0xa823b0+52,alternate_font);
        p.memory.store(text + 4,text_word,16);
        p.memory.copy_in(stack_base,stack);
        p.memory.copy_in(output,initial_output);
        p.memory.copy_in(0xb34500,initial_globals);
    };
    enum class Case {plain,outline,finite_reveal,changed_inputs,changed_font,same_metrics,shifted_stack,dirty_fill,dirty_outline,
                     force_yield,font_fault};
    a::Program program(p.memory);
    const auto run = [&](Case which,bool optimized,unsigned salt=0) {
        restore();
        p.cpu = initial;
        p.cpu.fp.put16(0,0x007f);
        p.cpu.eip = 0x446500;
        a::text_layout_replay_enabled = optimized;
        auto call_args = args;
        call_args[12] = which == Case::finite_reveal ? 51 : 0xffffffffu;
        call_args[18] = which == Case::outline || which == Case::dirty_outline ? 1 : 0;
        if(which == Case::changed_inputs) {
            p.memory.store(text + 4,0xcb90,16);
            call_args[2] += 5;
            call_args[3] += 7;
            call_args[14] = 129;
        }
        if(which == Case::changed_font) p.memory.store(0xa823b1,font_width-1,8);
        if(which == Case::same_metrics) {
            p.memory.copy_in(0xa823b0+52,original_font);
            p.memory.store(output+0x50,1,16);
        }
        if(which == Case::shifted_stack) p.cpu.r[wr::ESP] = (initial.r[wr::ESP] & ~4095u) + 0x100;
        if(which == Case::dirty_fill || which == Case::dirty_outline) {
            p.cpu.r[wr::ESP] -= 4;
            p.cpu.fp.put16(2,3u << 11);
            p.cpu.fp.bytes[4] = 0;
            // FXSAVE serializes payloads and padding even in empty slots.
            // Change those bytes between a cache fill and the next replay.
            for(unsigned i=32;i<p.cpu.fp.bytes.size();++i)
                p.cpu.fp.bytes[i] = static_cast<std::uint8_t>(i*73u+salt);
        }
        if(which == Case::font_fault) p.memory.protect(0xa82000,4096,0);
        // Keep space below the synthetic stack even in the cross-page case.
        // Capture a 4-KiB window ending at the active source stack top.
        const U32 capture_base = p.cpu.r[wr::ESP] - 4096;
        constexpr U32 return_address = 0xfabcf00d;
        for(auto it=call_args.rbegin();it!=call_args.rend();++it) wr::push(p.cpu,p.memory,*it);
        wr::push(p.cpu,p.memory,return_address);
        Trace result{};
        Observer capture{p,capture_base,result,which == Case::force_yield};
        a::SpriteService sink(p.memory,Observer::submit,&capture);
        a::LeafServiceScope leaves{[](wr::Cpu&,wr::Memory&,void* process) {
            return static_cast<wr::Process*>(process)->dispatch_leaf_api();
        },&p};
        const auto deferrals = a::glyph_budget_deferrals;
        try {
            unsigned entries{};
            while(p.cpu.eip != return_address) {
                CHECK(++entries < 2048);
                if(!program.invoke_unmetered(p.cpu)) CHECK(p.dispatch_api());
                capture.checkpoint();
            }
        } catch(const wr::GuestFault& error) {
            result.fault.emplace(error.kind,error.address);
            capture.checkpoint();
        }
        if(which == Case::force_yield) CHECK(a::glyph_budget_deferrals > deferrals);
        p.memory.copy_out(output,result.output_and_cursor);
        p.memory.copy_out(0xb34500,result.globals);
        return result;
    };
    unsigned checked{};
    for(const auto& [which,name] : std::array{
            std::pair{Case::plain,"plain"}, std::pair{Case::outline,"outline"},
            std::pair{Case::finite_reveal,"fading tail"}, std::pair{Case::changed_inputs,"same address, new inputs"},
            std::pair{Case::changed_font,"font mutation"}, std::pair{Case::same_metrics,"same metrics, different font"},
            std::pair{Case::shifted_stack,"cross-page stack"},
            std::pair{Case::dirty_fill,"fill with dirty FP slots"}, std::pair{Case::dirty_outline,"outline with dirty FP slots"},
            std::pair{Case::force_yield,"glyph yield"}, std::pair{Case::font_fault,"font read fault"}}) {
        const auto reference = run(which,false);
        CHECK(bool(reference.fault) == (which == Case::font_fault));
        if(!reference.fault) CHECK(!reference.commands.empty());
        compare(reference,run(which,true),name);
        const bool dirty = which == Case::dirty_fill || which == Case::dirty_outline;
        const auto second_reference = dirty ? run(which,false,109) : reference;
        const auto hits = a::text_layout_replay_hits;
        compare(second_reference,run(which,true,109),name);
        if(which == Case::plain || which == Case::outline || dirty) CHECK(a::text_layout_replay_hits > hits);
        ++checked;
    }
    restore();
    p.cpu = saved_cpu;
    a::text_layout_replay_enabled = saved_enabled;
    return checked;
}
}
