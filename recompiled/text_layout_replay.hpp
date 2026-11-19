#pragma once
#include "text_layout_replay_control.hpp"
#include "fp_execution.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <memory>
#include <new>

namespace wa2::aot {
namespace text_layout {
using Word = wr::U32;
using Scope = wr::Memory::ExecutionScope;
constexpr Word outline_end = 0x4472ea, fill_end = 0x4473b3;

// All inputs read before being overwritten in the two verified source slices.
// X87 starts with an empty tag word; each arithmetic operand then comes from
// this glyph. Rounding mode, sticky exception bits and MXCSR still matter.
struct Inputs {
    Word sp, bp, x, y, size, opacity, metrics, color, outline, x87, mxcsr;
    bool operator==(const Inputs&) const = default;
};
struct Entry {
    Inputs key{};
    std::array<Word, 8> regs{};
    Word flags{}, defined{}, scratch{}, coordinate[3]{};
    std::array<std::uint8_t,48> args{};
    std::array<std::uint8_t,160> fp{};
    bool valid{};
};
using Table = std::array<Entry,1024>;
inline thread_local std::unique_ptr<Table> table;

// Each call to the SSE ftol body pushes a return address and EBP and writes
// an aligned 8-byte double. Over the two/four calls these writes and the
// argument pushes cover one contiguous interval, including the deepest slot.
inline unsigned arg_size(Word sp, bool outline) {
    return (outline ? 40u : 48u) - (sp & 4);
}

inline unsigned bucket(const Inputs& key) {
    Word v = key.bp * 0x9e3779b9u ^ std::rotl(key.x * 0x85ebca77u, 9)
           ^ std::rotl(key.y * 0xc2b2ae3du, 16) ^ key.color ^ (key.sp >> 2)
           ^ (key.outline * 0x27d4eb2fu) ^ std::rotl(key.size, 11);
    return (v ^ (v >> 16)) & (std::tuple_size_v<Table>-1);
}

inline void copy_out(Scope& scope, Word address, std::span<std::uint8_t> dest) {
    while (!dest.empty()) {
        const auto part = std::min<std::size_t>(4096-(address & 4095),dest.size());
        const auto& page = (*scope.native_pages()[address >> 22])[(address >> 12)&1023];
        std::memcpy(dest.data(),page.data+(address & 4095),part);
        dest = dest.subspan(part);
        address += static_cast<Word>(part);
    }
}

inline bool inputs(wr::Cpu& cpu, Scope& scope, Inputs& result) {
    if constexpr (std::endian::native != std::endian::little) return false;
    const Word sp = cpu.r[wr::ESP];
    if (sp < 48 || sp > 0xffffffffu-0x1a8 || (sp&3) || cpu.fp.bytes[4] ||
        (cpu.fp.get16(0)&0x3f)!=0x3f || (cpu.fp.get16(2)&0x8080) ||
        (cpu.fp.get32(24)&0x1f80)!=0x1f80) return false;
    constexpr unsigned rw = wr::Memory::Read|wr::Memory::Write;
    const Word first = sp-48, last = sp+0x1a7;
    const unsigned leading = std::min(last-first+1,4096u-(first&4095));
    const auto* a = fp::detail::operand_page(scope,first,leading,rw);
    const auto* b = a;
    if (first+leading <= last)
        b = fp::detail::operand_page(scope,first+leading,last-(first+leading)+1,rw);
    if (!a || !b || ((a->permissions|b->permissions)&wr::Memory::Execute)) return false;
    if (a != b && a->data == b->data) return false;
    const auto outside_stack = [&](Word address, unsigned count) {
        const auto* page = fp::detail::operand_page(scope,address,count,wr::Memory::Read);
        return page && page->data != a->data && page->data != b->data;
    };
    const auto sprite = scope.load<32>(sp+0x150);
    // The sprite may be a caller's stack object. Its index is read before
    // the first stack write, so sharing storage here is safe.
    if (sprite > 0xffffffffu-0x51 ||
        !fp::detail::operand_page(scope,sprite+0x50,2,wr::Memory::Read)) return false;
    const Word index = wr::sign_extend(scope.load<16>(sprite+0x50),16);
    const Word row = 0xa823b0u + index * 0x34u;
    if (!outside_stack(row+1,3) || !outside_stack(0xbc8310,4) ||
        !scope.load<32>(0xbc8310)) return false;
    const Word metrics = scope.load<8>(row+1) | (scope.load<16>(row+2)<<8);
    const bool outline = scope.load<32>(sp+0x198) != 0;
    const auto color_base = sp + (outline ? 0x19cu : 0x48u);
    const auto color = scope.load<8>(color_base) |
                       (scope.load<8>(color_base+(outline?4:8))<<8) |
                       (scope.load<8>(color_base+(outline?8:12))<<16);
    result = {sp,cpu.r[wr::EBP],scope.load<32>(sp+0x24),scope.load<32>(sp+0x1c),
              scope.load<32>(sp+0x28),scope.load<32>(sp+0x34),metrics,color,
              Word(outline),cpu.fp.get32(0),cpu.fp.get32(24)};
    return true;
}

inline void restore(const Entry& entry, wr::Cpu& cpu, Scope& scope) {
    const auto sp = entry.key.sp;
    const bool outline = entry.key.outline != 0;
    const auto args = arg_size(sp,outline);
    scope.write_bytes(sp-args,std::span(entry.args).first(args));
    scope.store<32>(sp+0x18,entry.scratch);
    scope.write_bytes(sp+0x3c,{reinterpret_cast<const std::uint8_t*>(entry.coordinate),
                               outline ? 8u : 12u});
    cpu.r = entry.regs;
    cpu.flags = (cpu.flags&~wr::STATUS_FLAGS) | entry.flags;
    cpu.defined_flags = (cpu.defined_flags&~wr::STATUS_FLAGS) | entry.defined;
    // Control and reserved byte 5 survive the original path. Logical stack
    // depth peaks at 2 (outline) or 5 (fill) before returning to the old TOP.
    // ST slots below that depth keep their old payload, including stale data
    // visible to FXSAVE. The original x87 kernel clears all their padding.
    std::memcpy(cpu.fp.bytes.data()+2,entry.fp.data()+2,3);
    std::memcpy(cpu.fp.bytes.data()+6,entry.fp.data()+6,26);
    const unsigned first = outline ? 6 : 3;
    for (unsigned i=0;i<first;++i) cpu.fp.clear_x87_padding(i);
    const unsigned offset = 32+16*first;
    std::memcpy(cpu.fp.bytes.data()+offset,entry.fp.data()+offset,160-offset);
}
}

class TextLayoutReplay {
    using Word = wr::U32;
    text_layout::Entry* pending_{};
public:
    const bool enabled{text_layout_replay_enabled.load(std::memory_order_relaxed)};
    bool recording() const { return pending_ != nullptr; }
    Word begin(wr::Cpu& cpu, wr::Memory::ExecutionScope& scope) {
        using namespace text_layout;
        pending_ = nullptr;
        Inputs key;
        if (!inputs(cpu,scope,key)) return 0;
        if (!table) table.reset(new(std::nothrow) Table{});
        if (!table) return 0;
        auto& entry = (*table)[bucket(key)];
        if (entry.valid && entry.key==key) {
            restore(entry,cpu,scope);
            ++text_layout_replay_hits;
            return key.outline ? outline_end : fill_end;
        }
        entry.valid = false;
        entry.key = key;
        pending_ = &entry;
        return 0;
    }
    void finish(wr::Cpu& cpu, wr::Memory::ExecutionScope& scope, Word end) {
        using namespace text_layout;
        if (!pending_) return;
        auto& entry = *pending_;
        pending_ = nullptr;
        const Word sp = entry.key.sp;
        const bool outline = entry.key.outline != 0;
        if (end != (outline ? outline_end : fill_end) ||
            cpu.r[wr::ESP] != sp-(outline ? 20u : 28u) || cpu.fp.bytes[4]) return;
        entry.regs = cpu.r;
        entry.flags = cpu.flags&wr::STATUS_FLAGS;
        entry.defined = cpu.defined_flags&wr::STATUS_FLAGS;
        entry.scratch = scope.load<32>(sp+0x18);
        entry.coordinate[0] = scope.load<32>(sp+0x3c);
        entry.coordinate[1] = scope.load<32>(sp+0x40);
        if (!outline) entry.coordinate[2] = scope.load<32>(sp+0x44);
        const auto args = arg_size(sp,outline);
        copy_out(scope,sp-args,std::span(entry.args).first(args));
        std::memcpy(entry.fp.data(),cpu.fp.bytes.data(),entry.fp.size());
        entry.valid = true;
        ++text_layout_replay_captures;
    }
};
}
