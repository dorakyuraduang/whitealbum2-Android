#pragma once
#include "../recompiled/geometry_cache_detail.hpp"
inline unsigned geometry_cache_snapshots(){
    using Memo=wa2::aot::detail::GeometryCache<24,2,1>;
    Memo memo;unsigned checks{};std::array<std::uint8_t,24> input{};std::array<std::uint8_t,48> output{},expected{};
    for(unsigned n=0;n<input.size();++n)input[n]=std::uint8_t(n*13+7);
    for(unsigned n=0;n<expected.size();++n)expected[n]=std::uint8_t(n*17+3);
    wr::Cpu base;for(unsigned n=0;n<8;++n)base.r[n]=n*0x1234567u;
    base.eip=0x43cc17;for(unsigned n=0;n<160;++n)base.fp.bytes[n]=std::uint8_t(n*19+5);
    auto result=base;result.r[0]^=0x12345678;result.eip=0x43d5d7;result.flags^=0x805;result.defined_flags^=0x10;
    for(unsigned n=0;n<160;++n)result.fp.bytes[n]^=std::uint8_t(n*11+9);
    const auto prime=[&]{Memo::Pending pending{};auto cpu=base;output.fill(0);const bool hit=memo.lookup(cpu,input,output,pending);
        if(!hit)memo.commit(pending,result,expected);};
    prime();auto cpu=base;Memo::Pending pending{};output.fill(0);CHECK(memo.lookup(cpu,input,output,pending));
    CHECK(output==expected && cpu.r==result.r && cpu.eip==result.eip && cpu.flags==result.flags && cpu.defined_flags==result.defined_flags && cpu.fp.bytes==result.fp.bytes);++checks;
    // These transactions do not touch segment or SIMD state. A cache hit
    // must retain a caller's different values there, rather than restoring
    // another caller's complete CPU object.
    cpu=base;cpu.fs_base=123;cpu.gs_base=456;cpu.fs_valid=cpu.gs_valid=true;cpu.selectors.fill(17);
    for(unsigned n=160;n<cpu.fp.bytes.size();++n)cpu.fp.bytes[n]=std::uint8_t(n*29+3);
    const auto before=cpu;CHECK(memo.lookup(cpu,input,output,pending));
    CHECK(cpu.fs_base==before.fs_base && cpu.gs_base==before.gs_base && cpu.fs_valid && cpu.gs_valid && cpu.selectors==before.selectors);
    for(unsigned n=160;n<cpu.fp.bytes.size();++n)CHECK(cpu.fp.bytes[n]==before.fp.bytes[n]);++checks;
    // Every input byte, GPR, x87 byte and observed flag participates in exact
    // matching. One bucket forces collisions and two-way eviction here.
    for(unsigned n=0;n<input.size();++n){prime();auto changed=input;changed[n]^=1;cpu=base;CHECK(!memo.lookup(cpu,changed,output,pending));++checks;}
    for(unsigned n=0;n<8+160+3;++n){prime();cpu=base;
        if(n<8)cpu.r[n]^=1;else if(n<168)cpu.fp.bytes[n-8]^=1;
        else if(n==168)cpu.flags^=1;else if(n==169)cpu.defined_flags^=1;else cpu.eip^=1;
        CHECK(!memo.lookup(cpu,input,output,pending));++checks;
    }
    prime();cpu=base;std::array<std::uint8_t,47> shorter{};CHECK(!memo.lookup(cpu,input,shorter,pending));++checks;
    // A pending miss that throws/declines is never committed or reused.
    cpu=base;CHECK(!memo.lookup(cpu,input,shorter,pending));++checks;
    // Read-before-write masks ignore unrelated scratch bytes and retain
    // unwritten caller bytes, while a change to an actual operand misses.
    wa2::aot::detail::GeometryCache<24,2,1,8> sparse;
    wa2::aot::detail::GeometryTrace<8> trace;trace.read(3,2);trace.write(0,2);trace.read(0,2);
    using Sparse=decltype(sparse);Sparse::Pending sparse_pending{};
    cpu=base;output.fill(9);CHECK(!sparse.lookup(cpu,input,output,sparse_pending));
    const auto reads=trace.inputs(24);const std::array<wa2::aot::detail::GeometryRange,1> writes{{{0,2}}};
    sparse.commit(sparse_pending,result,expected,reads,writes);
    auto noise=input;noise[0]^=17;noise[1]^=31;noise[6]^=23;cpu=base;output.fill(37);
    CHECK(sparse.lookup(cpu,noise,output,sparse_pending));CHECK(output[0]==expected[0] && output[1]==expected[1]);
    for(unsigned n=2;n<output.size();++n)CHECK(output[n]==37);++checks;
    noise[3]^=1;cpu=base;CHECK(!sparse.lookup(cpu,noise,output,sparse_pending));++checks;
    return checks;
}
