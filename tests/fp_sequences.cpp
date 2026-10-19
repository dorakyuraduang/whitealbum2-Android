// Exact oracle for every fixed pure-x87 sequence. The reference executes only
// the existing generated instruction bodies, never a second copy of the batch.
#include "fp_sequences.hpp"
#include <winrecomp/compiled.hpp>
#include <winrecomp/core.hpp>
#include <winrecomp/hash.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <random>
#include <set>

bool code_page_1032(wr::Cpu&,wr::Memory&,std::uint64_t&);
bool code_page_1082(wr::Cpu&,wr::Memory&,std::uint64_t&);
bool code_page_1083(wr::Cpu&,wr::Memory&,std::uint64_t&);
bool code_page_1084(wr::Cpu&,wr::Memory&,std::uint64_t&);
bool code_page_1085(wr::Cpu&,wr::Memory&,std::uint64_t&);
bool code_page_1110(wr::Cpu&,wr::Memory&,std::uint64_t&);
bool code_page_1129(wr::Cpu&,wr::Memory&,std::uint64_t&);
bool code_page_1133(wr::Cpu&,wr::Memory&,std::uint64_t&);
bool code_page_1134(wr::Cpu&,wr::Memory&,std::uint64_t&);
bool code_page_1136(wr::Cpu&,wr::Memory&,std::uint64_t&);
bool code_page_1139(wr::Cpu&,wr::Memory&,std::uint64_t&);
bool code_page_1140(wr::Cpu&,wr::Memory&,std::uint64_t&);
using namespace wr;
#define CHECK(x) do { if(!(x))throw std::runtime_error(#x); } while(false)
namespace {
bool reference(Cpu& s,Memory& m,std::uint64_t& budget) {
    switch(s.eip>>12) {
    case 0x408:return code_page_1032(s,m,budget);
    case 0x43a:return code_page_1082(s,m,budget);
    case 0x43b:return code_page_1083(s,m,budget);
    case 0x43c:return code_page_1084(s,m,budget);
    case 0x43d:return code_page_1085(s,m,budget);
    case 0x456:return code_page_1110(s,m,budget);
    case 0x469:return code_page_1129(s,m,budget);
    case 0x46d:return code_page_1133(s,m,budget);
    case 0x46e:return code_page_1134(s,m,budget);
    case 0x470:return code_page_1136(s,m,budget);
    case 0x473:return code_page_1139(s,m,budget);
    case 0x474:return code_page_1140(s,m,budget);
    default:return false;
    }
}
struct Case {
    Cpu cpu;
    std::uint64_t budget=10000;
    unsigned limit=4096,seed=1;
    U32 return_pc=0xffffffc0,corrupt=0,readonly=0,unmapped=0;
};
struct Result {
    Cpu cpu;
    std::vector<std::uint8_t> memory;
    std::uint64_t budget{},instructions{},batches{};
    std::string fault;
    double seconds{};
};
Result execute(const Image& image,const Wa2FpSequenceInfo& info,const Case& c,bool fast) {
    Memory m;std::set<U32> pages;
    for(auto at=info.pc&~0xfffu;at<info.end;at+=4096)pages.insert(at);
    for(const auto& operand:info.operands) {
        const auto at=wa2_fp_operand_address(c.cpu,operand);
        if(std::uint64_t(at)+operand.size>0x100000000ull)continue;
        pages.insert(at&~0xfffu);pages.insert((at+operand.size-1)&~0xfffu);
    }
    std::mt19937 random(c.seed);
    for(const auto page:pages) {
        std::array<std::uint8_t,4096> bytes{};
        for(auto& b:bytes)b=std::uint8_t(random());
        const bool code=page>=(info.pc&~0xfffu) && page<info.end;
        if(code) {
            const auto source=image.code(page).first(4096);std::copy(source.begin(),source.end(),bytes.begin());
        }
        m.map(page,4096,Memory::Read|Memory::Write|(code?Memory::Execute:0));m.copy_in(page,bytes);
    }
    if(c.corrupt)m.initialize(c.corrupt,std::array<std::uint8_t,1>{std::uint8_t(m.load(c.corrupt,8)^0xffu)});
    if(c.readonly && pages.contains(c.readonly))m.protect(c.readonly,4096,Memory::Read);
    if(c.unmapped && pages.contains(c.unmapped))m.decommit(c.unmapped,4096);
    auto cpu=c.cpu;auto budget=c.budget;Result result;
    const auto started=std::chrono::steady_clock::now();
    try {
        auto scope=m.execution_scope();
        if(fast && cpu.eip!=c.return_pc)if(const auto used=wa2_fp_sequence_batch(cpu,m,budget,c.limit,c.return_pc)) {
            CHECK(used==info.instructions && used<=c.limit);result.instructions+=used;++result.batches;
        }
        while(cpu.eip>=info.pc && cpu.eip<info.end && result.instructions<c.limit && cpu.eip!=c.return_pc) {
            tick(budget,cpu.eip);CHECK(reference(cpu,m,budget));++result.instructions;
        }
    }catch(const GuestFault& fault) {
        result.fault=std::to_string(int(fault.kind))+":"+std::to_string(fault.address);
    }
    result.seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
    result.cpu=cpu;result.budget=budget;
    for(const auto page:pages)if(page!=c.unmapped) {
        const auto offset=result.memory.size();result.memory.resize(offset+4096);
        m.copy_out(page,std::span(result.memory).subspan(offset,4096));
    }
    return result;
}
void equal(const Result& a,const Result& b,U32 entry,unsigned number) {
    if(a.cpu.fp.bytes!=b.cpu.fp.bytes)for(unsigned k=0;k<512;++k)if(a.cpu.fp.bytes[k]!=b.cpu.fp.bytes[k]) {
        std::cerr<<"entry "<<std::hex<<entry<<std::dec<<" case "<<number<<" FP byte "<<k
            <<" expected="<<unsigned(a.cpu.fp.bytes[k])<<" actual="<<unsigned(b.cpu.fp.bytes[k])<<'\n';break;
    }
    CHECK(a.cpu.r==b.cpu.r && a.cpu.eip==b.cpu.eip && a.cpu.flags==b.cpu.flags && a.cpu.defined_flags==b.cpu.defined_flags);
    CHECK(a.cpu.fp.bytes==b.cpu.fp.bytes && a.cpu.selectors==b.cpu.selectors);
    CHECK(a.budget==b.budget && a.instructions==b.instructions && a.memory==b.memory && a.fault==b.fault);
}
void rejected(const Result& result,U32 entry,unsigned variant,const char* reason) {
    if(result.batches) {
        std::cerr<<"unexpected batch entry="<<std::hex<<entry<<std::dec<<" variant="<<variant<<" reason="<<reason<<'\n';
        throw std::runtime_error("batch should have fallen back");
    }
}
Case make_case(const Wa2FpSequenceInfo& info,std::mt19937& random,unsigned variant) {
    Case c;c.cpu.eip=info.pc;c.seed=random();
    for(unsigned k=0;k<8;++k)c.cpu.r[k]=0x20000000+k*0x00100000+(random()%256);
    c.cpu.flags=random();c.cpu.defined_flags=random();
    const unsigned precision[]={0,2,3};
    c.cpu.fp.put16(0,std::uint16_t(0x7f|(precision[random()%3]<<8)|((random()%4)<<10)));
    const auto top=variant%8,live=variant%9;
    c.cpu.fp.put16(2,std::uint16_t((top<<11)|(random()&0x473f)));
    for(unsigned k=0;k<live;++k)c.cpu.fp.bytes[4]|=std::uint8_t(1u<<((top+k)&7));
    // Include noncanonical extended values to exercise any difference caused
    // by repeated FXRSTOR tag reconstruction in the instruction oracle.
    constexpr std::uint64_t significands[]={0,1,0x8000000000000000ull,0x8000000000000001ull,
        0xc000000000000001ull,0x4000000000000000ull,0xffffffffffffffffull};
    constexpr std::uint16_t exponents[]={0,1,0x3fff,0x7fff,0x8000,0xffff};
    for(unsigned k=0;k<8;++k) {
        c.cpu.fp.put64(32+k*16,significands[(variant+k)%std::size(significands)]);
        c.cpu.fp.put16(40+k*16,exponents[(variant+k)%std::size(exponents)]);
        for(unsigned n=10;n<16;++n)c.cpu.fp.bytes[32+k*16+n]=std::uint8_t(random());
    }
    c.cpu.fp.put64(8,0x412345);c.cpu.fp.put64(16,0x12345678);c.cpu.fp.put16(6,0x321);
    for(unsigned k=160;k<512;++k)c.cpu.fp.bytes[k]=std::uint8_t(random());
    return c;
}
}
int main(int argc,char** argv) {try {
    CHECK(argc==3);const auto image=Image::load(argv[1]);std::mt19937 random(0xf087);
    static constexpr U32 specialized_entries[]={
#include "specialized_entries.inc"
    };
    unsigned cases=0;std::uint64_t batches=0,instructions=0;double before_seconds=0,after_seconds=0;
    for(const auto& info:wa2_fp_sequences()) {
        for(const auto entry:specialized_entries)CHECK(!(info.pc<entry && entry<info.end));
        CHECK(wa2_fp_sequence_entry(info.pc));
        Decoder decoder;const auto first=decoder.decode(image.code(info.pc),info.pc);CHECK(first);
        const auto interior_pc=info.instructions==1?info.pc+1:first->next();
        CHECK(interior_pc>info.pc && interior_pc<info.end);
        for(unsigned variant=0;variant<26;++variant) {
            auto c=make_case(info,random,variant);
            constexpr std::uint16_t control_words[]={0x003f,0x00ff,0xff3f,0xffff,0x3f3f,0x557f,0xaa7f,0x027f};
            if(variant>=18)c.cpu.fp.put16(0,control_words[variant-18]);
            const auto a=execute(image,info,c,false),b=execute(image,info,c,true);
            equal(a,b,info.pc,cases++);batches+=b.batches;instructions+=b.instructions;
            before_seconds+=a.seconds;after_seconds+=b.seconds;
        }
        for(unsigned cut=0;cut<=info.instructions;++cut) {
            auto c=make_case(info,random,cut);c.budget=cut;
            equal(execute(image,info,c,false),execute(image,info,c,true),info.pc,cases++);
        }
        for(unsigned limit=0;limit<=info.instructions;++limit) {
            auto c=make_case(info,random,limit);c.limit=limit;
            const auto a=execute(image,info,c,false),b=execute(image,info,c,true);
            equal(a,b,info.pc,cases++);if(limit<info.instructions)rejected(b,info.pc,limit,"slice limit");
        }
        for(unsigned kind=0;kind<15;++kind) {
            auto c=make_case(info,random,kind);
            if(kind<6)c.cpu.fp.put16(0,std::uint16_t(c.cpu.fp.get16(0)&~(1u<<kind)));
            if(kind==6)c.corrupt=info.end-1;
            if(kind==7)c.return_pc=interior_pc;
            if(kind==8)c.cpu.r.fill(0x20000100); // distinct expressions may alias.
            if(kind==9)c.cpu.r.fill(0xfffffffc); // address and operand wrap.
            if((kind==10 || kind==11) && !info.operands.empty()) {
                const auto& operand=info.operands.front();
                if(operand.base>=0 && operand.index<0)c.cpu.r[operand.base]=0x20000ffeu - operand.displacement;
                const auto at=wa2_fp_operand_address(c.cpu,operand);
                if(kind==10)c.readonly=(at+operand.size-1)&~0xfffu;
                if(kind==11)c.unmapped=(at+operand.size-1)&~0xfffu;
            }
            if(kind==12)c.return_pc=info.pc+1;
            if(kind==13)c.return_pc=info.pc;
            if(kind==14)c.return_pc=info.end;
            const auto a=execute(image,info,c,false),b=execute(image,info,c,true);
            equal(a,b,info.pc,cases++);if(kind<8 || kind==12 || kind==13)rejected(b,info.pc,kind,"guard case");
        }
        // Redirect a writable symbolic operand onto a later code byte. The
        // batch must fall back so code changes are observed at the exact PC.
        for(const auto& operand:info.operands)if(operand.write && operand.base>=0 && operand.index<0) {
            auto c=make_case(info,random,0);c.cpu.r[operand.base]=info.end-2-operand.displacement;
            const auto a=execute(image,info,c,false),b=execute(image,info,c,true);
            equal(a,b,info.pc,cases++);rejected(b,info.pc,0,"code overlap");break;
        }
    }
    CHECK(batches>0);
    std::ofstream report(argv[2]);
    report<<"{\"schema\":\"wa2.fp-sequence-tests.v1\",\"sequences\":"<<wa2_fp_sequences().size()
        <<",\"cases\":"<<cases<<",\"exact_cpu_flags_fpu_memory_budget_and_faults\":true"
        <<",\"guest_instructions\":"<<instructions<<",\"before_seconds\":"<<before_seconds
        <<",\"after_seconds\":"<<after_seconds<<",\"speedup\":"<<before_seconds/after_seconds
        <<",\"batch_count\":"<<batches<<"}\n";CHECK(report);
    std::cout<<"PASS "<<cases<<" exact cases over "<<wa2_fp_sequences().size()<<" FP sequences; "
        <<before_seconds<<" -> "<<after_seconds<<" seconds ("<<before_seconds/after_seconds<<"x)\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
