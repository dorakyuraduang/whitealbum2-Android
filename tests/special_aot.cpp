#include "static_program.hpp"
#include "bodies.hpp"
#include "probe_entries.hpp"
#include <iostream>
#include <fstream>
#include <random>

namespace s=wa2::special;
namespace s2=wa2::aot;
namespace wa2::special {bool probe_oracle(wr::Cpu&,wr::Memory&);}
#define CHECK(value) do{if(!(value))throw std::runtime_error("check failed: " #value);}while(false)

int main(int argc,char** argv){try{
    CHECK(argc==2);
    const auto image=s::initial_image();
    CHECK(image.static_input_identity=="1a93d3126a90c68d07c0f79ec19e3b282b3084538e1f92c3456525ba93103ae1");
    CHECK(image.imports.size()==224);
    wr::Memory actual,expected;
    const auto initialize=[&](wr::Memory& memory){
        memory.map(image.base,(image.size+4095u)&~4095u,wr::Memory::Read|wr::Memory::Write);
        for(const auto& section:image.sections){
            const auto address=image.base+section.rva;
            if(section.raw_size)memory.initialize(address,std::span(image.bytes).subspan(section.raw_offset,section.raw_size));
            if(section.characteristics&0x20000000u)
                memory.protect(address,(std::max(section.virtual_size,section.raw_size)+4095u)&~4095u,wr::Memory::Read|wr::Memory::Execute);
        }
        memory.map(0x07000000u,4096,wr::Memory::Read|wr::Memory::Write);
    };
    initialize(actual);initialize(expected);
    std::size_t spans{},bytes{},starts{},interiors{};
    for(const auto& f:s::functions()){
        std::size_t ordinal{};
        for(const auto& span:f.code){
            CHECK(actual.matches_code(span.address,span.bytes));
            for(std::size_t offset=0;offset<span.bytes.size();++offset){
                const auto pc=span.address+wr::U32(offset);const auto location=s::locate(pc);
                if(std::binary_search(std::begin(s::instruction_starts),std::end(s::instruction_starts),pc)){
                    CHECK(location.function==&f && location.resume==ordinal);++ordinal;++starts;
                }else{CHECK(!location.function);++interiors;}
            }
            bytes+=span.bytes.size();++spans;
        }
    }
    CHECK(starts==139324 && interiors>starts);
    s2::StaticProgram program(actual,s::functions(),s::locate);
    wr::Cpu unknown;unknown.eip=0xdeadbeef;const auto saved=unknown;
    CHECK(!program.invoke(unknown));CHECK(unknown.r==saved.r && unknown.eip==saved.eip);
    std::mt19937 random(0x57413253);std::size_t comparisons{};
    for(const auto entry:s::probe_entries)for(unsigned vector=0;vector<128;++vector){
        wr::Cpu cpu;
        for(auto& reg:cpu.r)reg=random();
        cpu.r[wr::ESP]=0x07000800u;cpu.eip=entry;cpu.flags=(random()&wr::STATUS_FLAGS)|2u;cpu.defined_flags=wr::STATUS_FLAGS;
        auto reference=cpu;
        constexpr auto sentinel=0xfefefefeu;
        actual.store(cpu.r[wr::ESP],sentinel,32);expected.store(cpu.r[wr::ESP],sentinel,32);
        CHECK(program.invoke(cpu));CHECK(cpu.eip==sentinel);
        for(unsigned steps=0;reference.eip!=sentinel;++steps){CHECK(steps<64);CHECK(s::probe_oracle(reference,expected));}
        CHECK(cpu.r==reference.r && cpu.eip==reference.eip && cpu.flags==reference.flags && cpu.defined_flags==reference.defined_flags);
        CHECK(cpu.fp.bytes==reference.fp.bytes);++comparisons;
    }
    CHECK(comparisons>=1024);
    // A writeable code mapping revokes prior proofs even if bytes still match.
    const auto first=s::probe_entries[0];const auto page=first&~4095u;
    actual.protect(page,4096,wr::Memory::Read|wr::Memory::Write|wr::Memory::Execute);
    wr::Cpu changed;changed.eip=first;changed.r[wr::ESP]=0x07000800;
    bool writable_rejected{};try{program.invoke(changed);}catch(const wr::GuestFault& f){writable_rejected=f.kind==wr::FaultKind::unsupported;}
    CHECK(writable_rejected);
    actual.store(first,actual.load(first,8)^1u,8);
    actual.protect(page,4096,wr::Memory::Read|wr::Memory::Execute);
    bool changed_rejected{};try{program.invoke(changed);}catch(const wr::GuestFault& f){changed_rejected=f.kind==wr::FaultKind::unsupported;}
    CHECK(changed_rejected);
    std::ofstream report(argv[1]);
    report<<"{\"schema\":\"wa2.special-memory-check.v1\",\"static_bodies\":"<<s::functions().size()
          <<",\"code_spans\":"<<spans<<",\"code_bytes\":"<<bytes<<",\"instruction_starts_checked\":"<<starts<<",\"interior_targets_rejected\":"<<interiors<<",\"differential_vectors\":"<<comparisons
          <<",\"unknown_target_rejected\":true,\"writable_code_rejected\":true,\"changed_code_rejected\":true,"
            "\"source_executable_opened\":false,\"game_launched\":false,\"screen_access\":false,\"complete_gameplay_verified\":false,\"passed\":true}\n";
    CHECK(bool(report));
    std::cout<<"PASS Special static catalogue and "<<comparisons<<" finite differential vectors. No game startup.\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
