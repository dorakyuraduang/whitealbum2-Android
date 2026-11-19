#pragma once
#include "gpr_registers.hpp"
#include <atomic>
#include <bit>
#include <cstring>

namespace wa2::aot {
inline std::atomic<bool> compact_text_advance_enabled{false};
inline thread_local std::uint64_t compact_text_advance_hits{};

struct TextAdvanceScope {
    const bool enabled{compact_text_advance_enabled.load(std::memory_order_relaxed)};
    unsigned hits{};
    ~TextAdvanceScope() { if (hits) compact_text_advance_hits += hits; }
};

// The ordinary double-byte glyph has already been submitted at this point.
// Both source passes converge here. A wrap or page overflow rejoins its
// original handler with every register and the final CMP flags in place.
template<class Registers>
inline wr::U32 compact_text_advance(wr::Cpu& cpu, wr::Memory::ExecutionScope& scope,
                                    Registers& regs) {
    if constexpr (std::endian::native != std::endian::little) return 0;
    const auto sp = regs.template get<wr::ESP>();
    constexpr unsigned first = 0x1c, end = 0x17c;
    if (sp > 0xffffffffu - (end - 1)) return 0;
    const auto address = sp + first;
    const auto* index = scope.native_pages()[address >> 22];
    if (!index) return 0;
    const auto& page = (*index)[(address >> 12) & 1023];
    const unsigned offset = address & 4095;
    constexpr unsigned rw = wr::Memory::Read | wr::Memory::Write;
    if (!page.data || (page.permissions & (rw | wr::Memory::Execute)) != rw ||
        offset + end - first > page.length) return 0;
    const auto* bytes = page.data + offset;
    const auto load = [&](unsigned slot) {
        wr::U32 word;
        std::memcpy(&word, bytes + (slot - first), sizeof(word));
        return word;
    };
    const auto signed_ge = [](wr::U32 a, wr::U32 b) {
        return std::bit_cast<std::int32_t>(a) >= std::bit_cast<std::int32_t>(b);
    };

    const auto spacing = load(0x164);
    const auto size = load(0x28);
    const auto x = load(0x24) + size + spacing;
    // Keep the checked store API, including invalidation of static page proofs.
    scope.store<32>(sp + 0x24, x);
    scope.store<32>(sp + 0x3c, 0);
    const auto base_size = load(0x178);
    const auto origin_x = load(0x154);
    const auto width_limit = (spacing + base_size) * load(0x15c) - size - spacing + 1;
    const auto dx = x - origin_x;
    regs.template set<wr::EBP,32,0>(regs.template get<wr::EBP>() + 1);
    regs.template set<wr::ESI,32,0>(size);
    regs.template set<wr::EBX,32,0>(origin_x);
    if (signed_ge(dx, width_limit)) {
        regs.template set<wr::EAX,32,0>(spacing);
        regs.template set<wr::ECX,32,0>(width_limit);
        regs.template set<wr::EDX,32,0>(dx);
        regs.template set<wr::EDI,32,0>(x);
        wr::sub(cpu, dx, width_limit, 32);
        return 0x446973;
    }
    const auto gap = load(0x168);
    const auto height_limit = (gap + base_size) * load(0x160) - size - gap + 1;
    const auto dy = load(0x1c) - load(0x158);
    const auto overflow = signed_ge(dy, height_limit);
    regs.template set<wr::EAX,32,0>(gap);
    regs.template set<wr::ECX,32,0>(height_limit);
    regs.template set<wr::EDX,32,0>(dy);
    regs.template set<wr::EDI,32,0>(overflow ? x : load(0x58));
    wr::sub(cpu, dy, height_limit, 32);
    return overflow ? 0x44779d : 0x446614;
}
}
