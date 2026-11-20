// Exercise the linked production AOT body against the unchanged cached loop.
#include "../generated/shared-aot/bodies.hpp"
#include "save_thumbnail.hpp"
#include <fstream>
#include <iostream>

namespace a=wa2::aot;
using wr::U32;
#define CHECK(expr) do{if(!(expr))throw std::runtime_error("thumbnail check at "+std::to_string(__LINE__));}while(false)

template<bool Metered=false>
static void reference(wr::Cpu& s,wr::Memory::ExecutionScope& scope){
    std::uint64_t budget{};
    goto L_43feb6;
#include "../generated/shared-aot/native-text/save_thumbnail_reference.inc"
L_43fee4:
    // Stop at the same first instruction after the loop as the product body.
    // ESI points at an unmapped test resource in both runs.
    s.eip=0x43fee4;s.r[wr::EAX]=scope.load<32>(s.r[wr::ESI]+0x70);
    throw std::logic_error("Fixture did not stop at the upload boundary");
}
enum class Shape {full,partial,unaligned,unindexed,pitch,height,width,overlap,stack_alias,source_fault,dest_fault,executable};
struct Result {
    wr::Cpu cpu;
    std::vector<std::uint8_t> memory;
    U32 fault{};
    std::uint64_t accelerated{},bytes{},epoch{};
};
static Result run(bool product,bool linear,Shape shape){
    constexpr U32 size=0x25000;
    const U32 base=0x2000000u+(shape==Shape::unindexed?1:0),stack=base+0x200;
    wr::Memory memory;if(linear)CHECK(memory.enable_linear_memory());memory.map(base,size,3);
    Result r;auto& s=r.cpu;
    r.memory.resize(size);
    for(unsigned i=0;i<size;++i)r.memory[i]=std::uint8_t(i*71+(i>>7));
    memory.copy_in(base,r.memory);
    const U32 src=base+0x1000+(shape==Shape::unaligned?3:0);
    U32 dst=base+0x13000+(shape==Shape::unaligned?7:0);
    if(shape==Shape::overlap)dst=src+4;
    if(shape==Shape::stack_alias)dst=stack+0x10;
    const U32 pitch=shape==Shape::pitch?260:512,height=shape==Shape::height?3:128;
    if(shape==Shape::stack_alias)memory.store(src,0,32);
    memory.store(stack+0x10,pitch,32);memory.store(stack+0x24,height,32);
    s.eip=0x43feb6;s.r[wr::ESP]=stack;s.r[wr::EAX]=shape==Shape::width?0:pitch/4;
    s.r[wr::EBX]=shape==Shape::partial?30:shape==Shape::stack_alias?127:0;
    s.r[wr::EDI]=src;s.r[wr::EBP]=dst;s.r[wr::ESI]=0x9000000;
    s.r[wr::ECX]=0x12340000;s.r[wr::EDX]=0x76540000;
    s.flags=0x6d7;s.defined_flags=0xff;s.fs_base=12;s.gs_base=34;s.fs_valid=s.gs_valid=true;
    for(unsigned i=0;i<s.fp.bytes.size();++i)s.fp.bytes[i]=std::uint8_t(i*31);
    if(shape==Shape::dest_fault)memory.protect(dst+4096,4096,wr::Memory::Read);
    if(shape==Shape::source_fault)memory.protect(src+4096,4096,wr::Memory::Write);
    if(shape==Shape::executable)memory.protect(dst+4096,4096,7);
    const auto before=a::save_thumbnail_counts;
    {
        auto scope=memory.execution_scope();
        try{
            if(product){
                const auto location=a::locate(s.eip);CHECK(location.function && location.function->entry==0x449590);
                std::uint64_t budget{};location.function->body(s,memory,scope,budget,false,location.resume);
            }else reference(s,scope);
            throw std::logic_error("Missing test memory boundary");
        }catch(const wr::GuestFault& f){CHECK(f.kind==wr::FaultKind::memory);r.fault=f.address;}
    }
    r.accelerated=a::save_thumbnail_counts.copies-before.copies;r.bytes=a::save_thumbnail_counts.bytes-before.bytes;
    r.epoch=memory.code_epoch();memory.protect(base,size,3);memory.copy_out(base,r.memory);return r;
}
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{},hits{};
    for(bool linear:{false,true})for(auto shape:{Shape::full,Shape::partial,Shape::unaligned,Shape::unindexed,Shape::pitch,Shape::height,
        Shape::width,Shape::overlap,Shape::stack_alias,Shape::source_fault,Shape::dest_fault,Shape::executable}){
        auto old=run(false,linear,shape),actual=run(true,linear,shape);
        const auto& x=old.cpu;const auto& y=actual.cpu;
        CHECK(x.r==y.r && x.eip==y.eip && x.flags==y.flags && x.defined_flags==y.defined_flags && x.fp.bytes==y.fp.bytes);
        CHECK(x.selectors==y.selectors && x.fs_base==y.fs_base && x.gs_base==y.gs_base && x.fs_valid==y.fs_valid && x.gs_valid==y.gs_valid);
        CHECK(old.fault==actual.fault && old.epoch==actual.epoch && old.memory==actual.memory && !old.accelerated);
        // The guard runs at the start of each row. After 16 rows the
        // executable-page case has passed both the first page and the
        // executable page, so its remaining 112 ordinary rows qualify.
        unsigned rows{};
        switch(shape){
        case Shape::full:case Shape::unaligned: rows=128;break;
        case Shape::partial: rows=98;break;
        case Shape::executable: rows=112;break;
        default: break;
        }
        if(actual.accelerated!=unsigned(rows!=0) || actual.bytes!=rows*512u){
            std::cerr<<"shape "<<int(shape)<<", linear "<<linear<<", copies "<<actual.accelerated
                     <<", bytes "<<actual.bytes<<", expected rows "<<rows<<'\n';
            throw std::runtime_error("Unexpected save thumbnail acceleration coverage");
        }
        hits+=unsigned(rows!=0);++cases;
    }
    std::ofstream out(argv[1]);out<<"{\"cases\":"<<cases<<",\"hits\":"<<hits
        <<",\"exact_cpu_ram_and_faults\":true,\"game_launched\":false,\"passed\":true}\n";
    CHECK(out.good());std::cout<<"PASS "<<cases<<" source/production thumbnail comparisons\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
