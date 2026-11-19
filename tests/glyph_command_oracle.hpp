#pragma once
#include "../recompiled/glyph_command_replay_control.hpp"

namespace glyph_command_test {
using novel_layout_test::Command;
using novel_layout_test::CpuImage;

struct Outcome {
    CpuImage cpu;
    std::array<std::uint8_t,0x128> stack;
    std::optional<Command> command;
    unsigned remaining{};
    bool returned{};
    bool operator==(const Outcome&) const = default;
};

// Use the public glyph entry both with and without command reuse. This
// captures the command before the presentation adapter consumes its span.
inline unsigned verify(wr::Memory& mem,const wr::Cpu& initial,U32 data) {
    const U32 sp = initial.r[wr::ESP];
    const U32 resource = 0xa93fb8, pointer_slot = resource+0x70;
    const U32 textures = mem.load(pointer_slot,32);
    const bool saved_enabled = a::glyph_command_replay_enabled;
    auto incoming = initial;
    struct Sink {
        Outcome& result;
        bool accept;
        static bool submit(const a::SpriteCommand& c,void* opaque) {
            auto& self = *static_cast<Sink*>(opaque);
            CHECK(!self.result.command);
            self.result.command.emplace(Command::copy(c));
            return self.accept;
        }
    };
    const auto execute = [&](bool optimized,unsigned allowance=3,bool accept=true) {
        a::glyph_command_replay_enabled = optimized;
        auto cpu = incoming;
        a::GlyphCallScope budget(mem,allowance);
        Outcome result{};
        Sink observer{result,accept};
        a::SpriteService sink(mem,Sink::submit,&observer);
        result.returned = a::native_draw_glyph_or_yield(cpu,mem);
        result.cpu.value = cpu;
        result.remaining = *a::GlyphCallScope::remaining(mem);
        mem.copy_out(sp,result.stack);
        return result;
    };
    unsigned cases{};
    const auto compare = [&](const char* name,unsigned allowance=3,bool accept=true) {
        const auto original = execute(false,allowance,accept);
        for(unsigned run=0;run<3;++run) {
            if(execute(true,allowance,accept) != original)
                throw std::runtime_error(std::string("Glyph command oracle: ")+name);
        }
        ++cases;
        return original;
    };
    const auto initial_hits = a::glyph_command_replay_hits;
    const auto baseline = compare("initial target");
    CHECK(a::glyph_command_replay_hits > initial_hits && baseline.returned && baseline.remaining == 2);
    CHECK(baseline.command && baseline.command->tiles.size() == 1);
    const auto warm_baseline = [&] {
        for(unsigned run=0;run<3;++run) CHECK(execute(true) == baseline);
    };
    const auto change = [&](const char* name,U32 at,unsigned width,U32 value) {
        warm_baseline();
        const U32 old = mem.load(at,width);
        mem.store(at,value,width);
        auto result = compare(name);
        mem.store(at,old,width);
        return result;
    };
    const auto selected_texture = baseline.command->tiles.front().texture;
    CHECK(selected_texture >= data+0x8000 && (selected_texture-data-0x8000)%16 == 0);
    const auto index = (selected_texture-data-0x8000)/16;
    CHECK(index < 195);
    const auto handle = textures+4*index;
    const auto remapped = change("texture changed in place",handle,32,selected_texture+0x8000);
    CHECK(remapped.command && remapped.command->tiles.front().texture == selected_texture+0x8000);
    const auto empty = change("texture cleared in place",handle,32,0);
    CHECK(empty.command && empty.command->tiles.empty());
    const auto recolored = change("new text color",sp+0x120,32,0x90808080);
    CHECK(recolored.command && recolored.command != baseline.command);
    const auto moved = change("sprite moved in place",sp+4+0x88,32,0x41000000);
    CHECK(moved.command && moved.command != baseline.command);
    change("font style cell",0xa823b3,8,28);
    change("palette entry",0x4c2290,32,0x01020408);
    change("font resource dimensions",resource+0x60,16,128);
    const auto disabled = change("invalidated source texture",resource,8,0);
    CHECK(!disabled.returned && !disabled.command && disabled.remaining == 3);
    const U32 target_slot = mem.load(0xb34428,32) ? 0xb3443c : 0xb34000;
    const auto retarget = change("surface target",target_slot,32,data+0x7000);
    CHECK(retarget.command && retarget.command->fields[1] == data+0x7000);
    change("device handle",0xb34438,32,data+0x6000);

    std::array<std::uint8_t,256> table;
    mem.copy_out(0x4a14b8,table);
    wa2::text::GlyphIndex lookup;
    unsigned crossing{};
    for(unsigned code=0x8980;!crossing && code<=0xeafc;++code) {
        const auto cell=lookup.lookup(table,static_cast<std::uint16_t>(code));
        if(!cell || (*cell/80)*40+32>=3840) continue;
        if(((*cell%80)*40+4)%256+28>256 || ((*cell/80)*40+4)%256+28>256)
            crossing=(code>>8)|((code&255)<<8);
    }
    CHECK(crossing);
    const auto split=change("atlas tile crossing",sp+0x104,32,crossing);
    CHECK(split.command && split.command->tiles.size() >= 2);
    const auto original_code = mem.load(sp+0x104,32);
    mem.store(sp+0x104,crossing,32);
    for(unsigned n=0;n<3;++n) CHECK(execute(true)==split);
    const auto second = split.command->tiles[1].texture;
    const auto second_slot = textures+4*((second-data-0x8000)/16);
    mem.store(second_slot,0,32);
    const auto partial = compare("one tile of a crossing disappears");
    CHECK(partial.command && partial.command->tiles.size()+1 == split.command->tiles.size());
    mem.store(second_slot,second,32);
    for(unsigned n=0;n<3;++n) CHECK(execute(true)==split);
    // Wider precision crossing arithmetic must retain its source fallback,
    // even when the same two-tile request has a hot PC24 entry.
    incoming.fp.put16(0,0x027f);
    const auto wider = compare("cached crossing changes precision");
    CHECK(!wider.returned && !wider.command && wider.remaining == 3 && wider.cpu == CpuImage{incoming});
    incoming = initial;
    mem.store(sp+0x104,original_code,32);

    // Mutate the table bytes in this private fixture only, preserving the
    // guest address. Restore its content and original permissions afterward.
    constexpr U32 ranges = 0x4a14b8, range_page = ranges & ~4095u;
    warm_baseline();
    const auto permissions=mem.permissions(range_page);
    mem.protect(range_page,4096,permissions | wr::Memory::Write);
    mem.store(ranges,0,16);mem.store(ranges+2,255,16);
    mem.store(ranges+4,0xffff,16);mem.store(ranges+6,0xffff,16);
    compare("range table changed in place");
    mem.copy_in(ranges,table);mem.protect(range_page,4096,permissions);

    // This handle is readable but the remainder of its declared table is
    // protected. A native decline must preserve the pending source call.
    warm_baseline();
    const U32 guard = data+0x5000;
    const auto guard_permissions = mem.permissions(guard);
    const U32 replacement = guard-4-index*4;
    const U32 saved_handle = mem.load(guard-4,32);
    mem.store(guard-4,selected_texture,32);
    mem.store(pointer_slot,replacement,32);
    CHECK(execute(false).returned);
    for(unsigned n=0;n<3;++n) execute(true);
    mem.protect(guard,4096,0);
    const auto blocked = compare("unreadable suffix of the handle table");
    CHECK(!blocked.command && !blocked.returned && blocked.cpu == CpuImage{initial} && blocked.remaining == 3);
    mem.protect(guard,4096,guard_permissions);
    mem.store(pointer_slot,textures,32);mem.store(guard-4,saved_handle,32);

    warm_baseline();
    const auto denied=compare("adapter declines a warmed command",3,false);
    CHECK(denied.command == baseline.command && !denied.returned && denied.remaining == 3 &&
          denied.cpu == CpuImage{initial} && denied.stack == baseline.stack);
    const auto yielded=compare("exhausted glyph allowance",0);
    CHECK(!yielded.command && yielded.returned && yielded.remaining == 0 &&
          yielded.cpu == CpuImage{initial} && yielded.stack == baseline.stack);
    a::glyph_command_replay_enabled = saved_enabled;
    return cases;
}
}
