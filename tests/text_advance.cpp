#include "../recompiled/text_advance.hpp"
#include "../recompiled/integer_value.hpp"
#include <array>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <random>

namespace a = wa2::aot;
using wr::U32;
#define CHECK(test) do { if (!(test)) throw std::runtime_error("advance check at " + std::to_string(__LINE__)); } while(false)

// Exact contiguous sequences from the immutable cached source. Every exit
// below is the first instruction *after* the region under comparison.
template<bool Metered = false, class Registers>
static U32 source_advance(wr::Cpu& s, wr::Memory::ExecutionScope& scope, Registers& integer) {
    std::uint64_t budget = 1000;
    goto L_44751b;
#include "../generated/shared-aot/native-text/text_advance_reference.inc"
L_446973: return 0x446973;
L_44779d: return 0x44779d;
L_446614: return 0x446614;
}

static bool equal(const wr::Cpu& x, const wr::Cpu& y) {
    return x.r == y.r && x.eip == y.eip && x.flags == y.flags &&
           x.defined_flags == y.defined_flags && x.fp.bytes == y.fp.bytes &&
           x.fs_valid == y.fs_valid && x.gs_valid == y.gs_valid &&
           x.fs_base == y.fs_base && x.gs_base == y.gs_base && x.selectors == y.selectors;
}

template<bool DirectRegisters>
static void compare(wr::Memory& mem, const wr::Cpu& initial, const std::array<std::uint8_t,4096>& page,
                    U32 base, std::array<unsigned,3>& exits) {
    auto original = initial, compact = initial;
    std::array<std::uint8_t,4096> wanted{}, actual{};
    mem.copy_in(base, page);
    {
        auto scope = mem.execution_scope();
        a::IntegerRegisters<DirectRegisters> registers(original, mem, scope);
        original.eip = source_advance(original, scope, registers);
    }
    mem.copy_out(base, wanted);
    mem.copy_in(base, page);
    {
        auto scope = mem.execution_scope();
        a::IntegerRegisters<DirectRegisters> registers(compact, mem, scope);
        compact.eip = a::compact_text_advance(compact, scope, registers);
    }
    mem.copy_out(base, actual);
    CHECK(equal(original, compact) && wanted == actual);
    if (compact.eip == 0x446614) ++exits[0];
    else if (compact.eip == 0x446973) ++exits[1];
    else if (compact.eip == 0x44779d) ++exits[2];
    else CHECK(false);
}

int main(int argc, char** argv) { try {
    const U32 base = 0x10000;
    wr::Memory memory;
    memory.map(base, 4096, wr::Memory::Read | wr::Memory::Write);
    std::mt19937 random(0x44751b);
    std::array<unsigned,3> exits{};
    std::array<std::uint8_t,4096> page{};
    wr::Cpu cpu;
    for (unsigned trial = 0; trial < 1024; ++trial) {
        for (auto& byte : page) byte = static_cast<std::uint8_t>(random());
        for (auto& word : cpu.r) word = random();
        for (auto& byte : cpu.fp.bytes) byte = static_cast<std::uint8_t>(random());
        cpu.flags = random(); cpu.defined_flags = random();
        cpu.eip = 0x44751b;
        const U32 offset = trial % 8 == 0 ? 4096 - 0x17c : random() % 0x900;
        cpu.r[wr::ESP] = base + offset;
        const auto put = [&](unsigned slot, U32 value) {
            std::memcpy(page.data() + offset + slot, &value, sizeof(value));
        };
        if (trial < 64) {
            // Place the resulting X or Y one unit to either side of the exact
            // threshold, including equality where the source takes JGE.
            put(0x154, 100); put(0x158, 30); put(0x164, 2); put(0x168, 4);
            put(0x178, 28); put(0x28, 28); put(0x15c, 4); put(0x160, 3);
            put(0x24, 100 + 91 - 30 + trial % 3 - 1);
            put(0x1c, 30 + 65 + (trial / 3) % 3 - 1);
        }
        compare<false>(memory, cpu, page, base, exits);
        compare<true>(memory, cpu, page, base, exits);
    }
    unsigned declines{};
    for (const auto permissions : {1u,3u,7u}) {
        memory.protect(base, 4096, permissions);
        for (auto sp : {base + 4096 - 0x17c + 1, base + 64, U32(0xffffff00)}) {
            if (permissions == 3 && sp == base + 64) continue;
            cpu.eip = 0x44751b; cpu.r[wr::ESP] = sp;
            const auto initial = cpu;
            std::array<std::uint8_t,4096> before{}, after{};
            memory.copy_out(base, before);
            {
                auto scope = memory.execution_scope();
                a::IntegerRegisters<false> registers(cpu, memory, scope);
                CHECK(a::compact_text_advance(cpu, scope, registers) == 0);
            }
            memory.copy_out(base, after);
            CHECK(equal(cpu, initial) && after == before);
            ++declines;
        }
    }
    CHECK(exits[0] && exits[1] && exits[2] && declines == 8);
    if (argc == 2) {
        std::ofstream report(argv[1]);
        report << "{\"exact_advances\":2048,\"declines\":" << declines
               << ",\"normal\":" << exits[0] << ",\"wrap\":" << exits[1]
               << ",\"page\":" << exits[2] << ",\"passed\":true}\n";
        CHECK(report);
    }
    std::cout << "2048 exact character advances, next/wrap/page exits "
              << exits[0] << '/' << exits[1] << '/' << exits[2]
              << ", 8 guard declines\n";
    return EXIT_SUCCESS;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return EXIT_FAILURE;
} }
