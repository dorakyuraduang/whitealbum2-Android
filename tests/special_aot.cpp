#include "static_program.hpp"
#include "bodies.hpp"
#include "probe_entries.hpp"
#include <iostream>
#include <fstream>
#include <random>
#include <cstring>

namespace s=wa2::special;
namespace s2=wa2::aot;
namespace wa2::special {bool probe_oracle(wr::Cpu&,wr::Memory&);}
#define CHECK(value) do{if(!(value))throw std::runtime_error("check failed: " #value);}while(false)

unsigned string_regression(s2::StaticProgram& program,wr::Memory& memory){
    // Resume at the reported REP only. A deliberately unmapped stack stops
    // execution at the following PUSH, before any game call or startup runs.
    constexpr wr::U32 instruction=0x479812,next_push=0x479817;
    constexpr wr::U32 source_page=0x07100000,target_page=0x07110000;
    constexpr wr::U32 source=source_page+4091,target=target_page+4093;
    constexpr unsigned words=136,bytes=words*4;
    memory.map(source_page,8192,wr::Memory::Read|wr::Memory::Write);
    memory.map(target_page,8192,wr::Memory::Read|wr::Memory::Write);
    std::array<std::uint8_t,bytes> input{},output{},blank{};blank.fill(0xa5);
    for(unsigned n=0;n<bytes;++n)input[n]=std::uint8_t(n*37+11);
    unsigned checked{};
    for(const auto count:{0u,words})for(bool backwards:{false,true}){
        memory.copy_in(source,input);memory.copy_in(target,blank);
        wr::Cpu cpu;cpu.eip=instruction;cpu.r[wr::ESP]=0;cpu.r[wr::EBX]=target;
        cpu.r[wr::ECX]=count;cpu.flags=2u|(backwards?wr::DF:0u);
        cpu.r[wr::ESI]=count?source+(backwards?bytes-4:0):0;
        cpu.r[wr::EDI]=target+(count && backwards?bytes-4:0);
        const auto initial_source=cpu.r[wr::ESI],initial_target=cpu.r[wr::EDI];
        bool stopped{};try{program.invoke(cpu);}catch(const wr::GuestFault& fault){
            if(fault.kind!=wr::FaultKind::memory || fault.address!=0xfffffffcu)throw;
            stopped=true;
        }
        CHECK(stopped && cpu.eip==next_push && cpu.r[wr::ECX]==0);
        const auto advance=backwards?0u-count*4:count*4;
        CHECK(cpu.r[wr::ESI]==initial_source+advance && cpu.r[wr::EDI]==initial_target+advance);
        CHECK(bool(cpu.flags&wr::DF)==backwards);
        memory.copy_out(target,output);auto expected=count?input:blank;
        // The one subsequent AND clears the first destination word.
        std::fill_n(expected.begin(),4,0);CHECK(output==expected);++checked;
    }
    // The unmetered fallback must retain a two-element prefix before an
    // actual read fault; it must not become a budget failure or atomic copy.
    memory.copy_in(target,blank);memory.copy_in(source_page+8192-8,std::span(input).first(8));
    wr::Cpu partial;partial.eip=instruction;partial.r[wr::ECX]=words;
    partial.r[wr::ESI]=source_page+8192-8;partial.r[wr::EDI]=target;partial.flags=2;
    bool faulted{};try{program.invoke(partial);}catch(const wr::GuestFault& fault){
        CHECK(fault.kind==wr::FaultKind::memory && fault.address==source_page+8192);faulted=true;
    }
    CHECK(faulted && partial.eip==instruction && partial.r[wr::ECX]==words-2);
    CHECK(partial.r[wr::ESI]==source_page+8192 && partial.r[wr::EDI]==target+8);
    memory.copy_out(target,output);auto expected=blank;std::copy_n(input.begin(),8,expected.begin());CHECK(output==expected);++checked;
    // Independent diagnostic execution still enforces its explicit budget.
    memory.copy_in(source,input);memory.copy_in(target,blank);
    wr::Cpu diagnostic;diagnostic.eip=instruction;diagnostic.r[wr::ECX]=words;
    diagnostic.r[wr::ESI]=source;diagnostic.r[wr::EDI]=target;diagnostic.flags=2;
    std::uint64_t budget=2;bool exhausted{};
    try{wr::aot_string_op<true>(diagnostic,memory,0,32,32,1,0,budget);}catch(const wr::GuestFault& fault){
        CHECK(fault.kind==wr::FaultKind::budget && fault.address==instruction);exhausted=true;
    }
    CHECK(exhausted && budget==0 && diagnostic.r[wr::ECX]==words-2);
    CHECK(diagnostic.r[wr::ESI]==source+8 && diagnostic.r[wr::EDI]==target+8);
    memory.copy_out(target,output);CHECK(output==expected);++checked;
    return checked;
}

unsigned copy_regression(s2::StaticProgram& program,wr::Memory& memory){
    // Both recovered CRT copy routines run solely against constructed RAM.
    // The caller owns arguments; a sentinel returns before any game caller.
    constexpr wr::U32 buffer=0x07200000,bytes=196608,stack=0x07000800,returned=0xfefefefe;
    memory.map(buffer,bytes,wr::Memory::Read|wr::Memory::Write);
    std::vector<std::uint8_t> input(bytes),expected(bytes),output(bytes);
    for(unsigned n=0;n<bytes;++n)input[n]=std::uint8_t(n*29+(n>>8)*7+17);
    const auto prior_feature=memory.load(0xbc54f0,32);memory.store(0xbc54f0,0,32);
    s2::FunctionWorkSamples samples(s::functions());s2::FunctionWorkScope work(&samples);
    unsigned cases{};
    const auto check=[&](wr::U32 entry,unsigned from,unsigned to,unsigned size){
        memory.copy_in(buffer,input);expected=input;std::memmove(expected.data()+to,expected.data()+from,size);
        wr::Cpu cpu;cpu.eip=entry;cpu.r[wr::ESP]=stack;cpu.r[wr::EBX]=0x12345678;
        cpu.r[wr::EBP]=0x33445566;cpu.r[wr::ESI]=0x778899aa;cpu.r[wr::EDI]=0xbbccddee;cpu.flags=2;
        const auto saved=cpu;memory.store(stack,returned,32);memory.store(stack+4,buffer+to,32);
        memory.store(stack+8,buffer+from,32);memory.store(stack+12,size,32);
        for(unsigned slices=0;cpu.eip!=returned;++slices){CHECK(slices<128);CHECK(program.invoke(cpu));}
        CHECK(cpu.r[wr::EAX]==buffer+to && cpu.r[wr::ESP]==stack+4 && !(cpu.flags&wr::DF));
        for(const auto reg:{wr::EBX,wr::EBP,wr::ESI,wr::EDI})CHECK(cpu.r[reg]==saved.r[reg]);
        memory.copy_out(buffer,output);CHECK(output==expected);++cases;
    };
    for(const auto entry:{0x474e20u,0x475190u}){
        for(unsigned source=0;source<4;++source)for(unsigned target=0;target<4;++target){
            for(unsigned size=0;size<=80;++size)check(entry,4096+source,102400+target,size);
            for(const auto size:{96u,127u,128u,255u,256u,511u,1023u,4093u,4096u,65537u})
                check(entry,4096+source,102400+target,size);
        }
        for(unsigned alignment=0;alignment<4;++alignment)for(const auto delta:{-3,-1,0,1,3}){
            const unsigned source=8192+alignment,target=unsigned(int(source)+delta);
            for(unsigned size=0;size<=40;++size)check(entry,source,target,size);
            for(const auto size:{127u,128u,4093u})check(entry,source,target,size);
        }
    }
    CHECK(samples.string_bulk_completed>0 && samples.string_bulk_bytes>=131072);
    // The CRT's initialized SSE2 selection must also retain the same result.
    memory.store(0xbc54f0,1,32);
    for(const auto entry:{0x474e20u,0x475190u})for(const auto size:{128u,4096u,65537u})
        check(entry,4096,102400,size);
    memory.store(0xbc54f0,prior_feature,32);return cases;
}

unsigned transcendental_regression(s2::StaticProgram& program,wr::Memory& memory){
    namespace fp=wa2::aot::fp;unsigned cases{};
    for(const auto precision:{0u,2u,3u})for(const auto exponent:{6u,7u,8u,9u,10u,11u,12u,13u}){
        wr::Cpu cpu;cpu.eip=4642967;cpu.r[wr::ESP]=0;cpu.fp.put16(0,std::uint16_t(0x7f|(precision<<8)));
        {fp::detail::Stack stack(&cpu.fp);stack.push(fp::detail::constant(5,0));stack.push(fp::from_integer(1ll<<exponent));}
        bool stopped{};try{program.invoke(cpu);}catch(const wr::GuestFault& fault){
            CHECK(fault.kind==wr::FaultKind::memory && fault.address==0);stopped=true;
        }
        CHECK(stopped && cpu.eip==0x46d8ab);fp::Environment env;
        const fp::Extended result{cpu.fp.get64(32),cpu.fp.get16(40)};
        CHECK(fp::to_binary(result,64,env)==std::bit_cast<std::uint64_t>(double(exponent)+0.5));++cases;
    }
    return cases;
}

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
    CHECK(starts==std::size(s::instruction_starts) && starts>=139324 && interiors>starts);
    for(const auto entry:s::recovered_callback_entries)CHECK(s::locate(entry).function);
    CHECK(std::size(s::recovered_callback_entries)>=65);
    CHECK(s::locate(4739667).function && s::locate(0x485211).function);
    for(const auto entry:s::recovered_jump_entries)CHECK(s::locate(entry).function);
    CHECK(std::size(s::recovered_jump_entries)>=26 && s::locate(4673248).function);
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
    const auto copy_cases=copy_regression(program,actual);
    const auto string_cases=string_regression(program,actual);
    const auto transcendental_cases=transcendental_regression(program,actual);
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
          <<",\"reported_rep_address\":4691986,\"string_regression_cases\":"<<string_cases<<",\"zero_budget_rep_words\":136,\"string_fault_prefix_checked\":true,\"diagnostic_budget_preserved\":true"
          <<",\"reported_callback_address\":4739667,\"recovered_callback_entries_checked\":"<<std::size(s::recovered_callback_entries)
          <<",\"reported_copy_address\":4673248,\"recovered_jump_entries_checked\":"<<std::size(s::recovered_jump_entries)<<",\"crt_copy_cases\":"<<copy_cases<<",\"shared_bulk_string_exercised\":true"
          <<",\"reported_transcendental_address\":4642967,\"generated_fyl2x_prefix_cases\":"<<transcendental_cases
          <<",\"unknown_target_rejected\":true,\"writable_code_rejected\":true,\"changed_code_rejected\":true,"
            "\"source_executable_opened\":false,\"game_launched\":false,\"screen_access\":false,\"complete_gameplay_verified\":false,\"passed\":true}\n";
    CHECK(bool(report));
    std::cout<<"PASS Special static catalogue and "<<comparisons<<" finite differential vectors. No game startup.\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
