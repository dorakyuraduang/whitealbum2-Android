#include "floating.hpp"
#include "fp_registers.hpp"
#include <fstream>
#include <iostream>
extern "C" void wr_x87_216_m0(void*,void*,std::uint32_t*);
extern "C" void wr_x87_217_m0(void*,void*,std::uint32_t*);
extern "C" void wr_x87_217_m3(void*,void*,std::uint32_t*);
extern "C" void wr_x87_217_m5(void*,void*,std::uint32_t*);
extern "C" void wr_sse_242_94_r10(void*,void*,std::uint32_t*);
extern "C" void wr_sse_102_111_m0(void*,void*,std::uint32_t*);
namespace f=wa2::aot::fp;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
struct Fault{bool raised{};wr::FaultKind kind{};std::uint32_t address{};bool operator==(const Fault&)const=default;};
Fault run(bool native,unsigned op,wr::Cpu& s,wr::Memory& m,unsigned address,bool cached=false){try{
    auto scope=m.execution_scope();f::RegisterFrame<false> registers(s,m,scope);
    switch(op){
    case 0:case 6:
        if(native)wr::x87_memory(s,m,wr_x87_216_m0,address,4,false,0);
        else if(cached)registers.memory<216,0,true>(address,4,false,0);
        else f::x87_memory<f::x87<216,0,true>>(s,m,address,4,false,0);break;
    case 1:
        if(native)wr::x87_memory(s,m,wr_x87_217_m0,address,4,false,0x100);
        else if(cached)registers.memory<217,0,true>(address,4,false,0x100);
        else f::x87_memory<f::x87<217,0,true>>(s,m,address,4,false,0x100);break;
    case 2:
        if(native)wr::x87_memory(s,m,wr_x87_217_m3,address,4,true,0x118);
        else if(cached)registers.memory<217,3,true>(address,4,true,0x118);
        else f::x87_memory<f::x87<217,3,true>>(s,m,address,4,true,0x118);break;
    case 3:case 7:
        if(native)wr::sse_execute(s,wr_sse_242_94_r10,nullptr);
        else f::sse_execute<f::sse<242,94,10,false,0>>(s,nullptr);break;
    case 4:
        if(native)wr::sse_memory(s,m,wr_sse_102_111_m0,address,16,false,16);
        else f::sse_memory<f::sse<102,111,0,true,0>>(s,m,address,16,false,16);break;
    case 5:
        if(native)wr::x87_memory(s,m,wr_x87_217_m5,address,2,false,0x128,true);
        else if(cached)registers.memory<217,5,true>(address,2,false,0x128,true);
        else f::x87_memory<f::x87<217,5,true>>(s,m,address,2,false,0x128,true);break;
    }
    return {};
}catch(const wr::GuestFault& e){return {true,e.kind,e.address};}}
Fault sequence(bool cached,wr::Cpu& cpu,wr::Memory& memory,unsigned variant){try{
    auto scope=memory.execution_scope();f::RegisterFrame<false> frame(cpu,memory,scope);
    cpu.eip=0x43ce19;
    if(cached)frame.memory<217,0,true>(0x1800,4,false,0x100);
    else wr::x87_memory(cpu,memory,wr_x87_217_m0,0x1800,4,false,0x100);
    cpu.eip+=4;
    if(cached)frame.memory<216,0,true>(0x1804,4,false,0x104);
    else wr::x87_memory(cpu,memory,wr_x87_216_m0,0x1804,4,false,0x104);
    wr::set_reg(cpu,wr::EAX,cached?frame.word(2):cpu.fp.get16(2),16);
    if(variant&1){
        cpu.eip+=4;std::uint32_t value=0x3eaaaaab;
        auto callback=[&]{wr::x87_execute(cpu,wr_x87_216_m0,&value,0x104);};
        if(cached)frame.external(callback);else callback();
    }
    cpu.eip+=4;
    if(cached)frame.memory<217,3,true>(variant&2?0x3000:variant&4?0x2800:0x1810,4,true,0x118);
    else wr::x87_memory(cpu,memory,wr_x87_217_m3,variant&2?0x3000:variant&4?0x2800:0x1810,4,true,0x118);
    return {};
}catch(const wr::GuestFault& error){return {true,error.kind,error.address};}}
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{},faults{},completed{},masked{},unmasked{};
    unsigned locked_stores{};
    for(unsigned protection:std::array<unsigned,3>{wr::Memory::Read|wr::Memory::Write|wr::Memory::Execute,wr::Memory::Read,wr::Memory::Write})
    for(unsigned count:{0u,1u,4u,8u,16u})for(unsigned address:{0x1001u,0x1ffcu,0x2ffeu,0xffffffffu}){
        wr::Memory actual,expected;std::array<std::uint8_t,16> bytes;
        for(unsigned n=0;n<bytes.size();++n)bytes[n]=std::uint8_t(17+n*29);
        for(auto* m:{&actual,&expected}){m->map(0x1000,8192,7);m->protect(0x2000,4096,protection);}
        Fault a,b;const auto epoch_a=actual.code_epoch(),epoch_b=expected.code_epoch();
        try{auto scope=actual.execution_scope();scope.write_bytes(address,std::span(bytes).first(count));}catch(const wr::GuestFault& e){a={true,e.kind,e.address};}
        try{expected.copy_in(address,std::span(bytes).first(count));}catch(const wr::GuestFault& e){b={true,e.kind,e.address};}
        CHECK(a==b && actual.code_epoch()-epoch_a==expected.code_epoch()-epoch_b);
        actual.protect(0x1000,8192,3);expected.protect(0x1000,8192,3);
        std::array<std::uint8_t,8192> left,right;actual.copy_out(0x1000,left);expected.copy_out(0x1000,right);CHECK(left==right);++locked_stores;
    }
    constexpr std::uint64_t values[]={0,0x8000000000000000ull,0x3ff0000000000000ull,0x4008000000000000ull,
        0x7ff0000000000000ull,1,0x7ff0000000000001ull,0x3fd5555555555555ull};
    constexpr std::uint32_t inputs[]={0,0x80000000,0x3f800000,0x40400000,0x7f800000,1,0x7f800001,0x3eaaaaab};
    wr::Memory left,right,register_memory;left.map(0x1000,0x2000,3);right.map(0x1000,0x2000,3);register_memory.map(0x1000,0x2000,3);
    for(unsigned op=0;op<8;++op)for(unsigned rc=0;rc<4;++rc)for(unsigned mask=0;mask<3;++mask)for(unsigned variant=0;variant<13;++variant){
        wr::Cpu actual;actual.eip=0x401000+op;actual.r[wr::EAX]=0x12345678;
        actual.fp.put16(0,std::uint16_t(0x340|(rc<<10)|(mask==0?0x3f:mask==1?0x3e:0x1f)));
        actual.fp.put32(24,(0x1f80|(rc<<13))&~(mask==1?0x80:mask==2?0x1000:0));actual.fp.bytes[4]=1;
        f::Environment env;const auto number=f::from_binary(values[variant%8],64,env);
        actual.fp.put64(32,number.significand);actual.fp.put16(40,number.sign_exponent);
        actual.fp.put64(176,values[variant%8]);actual.fp.put64(192,values[(variant+1)%8]);
        // Noncanonical reserved serialization bytes must normalize exactly as
        // the old helper, including for masked operations in the fast path.
        for(unsigned i=0;i<8;++i)std::fill_n(actual.fp.bytes.data()+42+i*16,6,0xa5);
        if(op==6){actual.fp.put16(0,0x37e);actual.fp.put16(2,1);}
        if(op==5 && variant==8)actual.fp.put16(2,1);
        if(op==7)actual.fp.put32(24,0x10000|0x1f80);
        auto expected=actual,register_cpu=actual;
        const unsigned address=variant==1?0x1801:variant==2 || variant==10?0x1fff:variant==11?0x3000:0x1800;
        for(auto* memory:{&left,&right,&register_memory}){
            memory->protect(0x1000,0x2000,3);
            std::array<std::uint8_t,0x2000> zero{};memory->copy_in(0x1000,zero);
            if(variant!=11)memory->store(address,op==5?(variant==8?0x37e:0x37f):inputs[variant%8],32);
            if(variant==2)memory->protect(0x2000,0x1000,wr::Memory::Read);
            if(variant==8)memory->protect(0x1000,0x1000,wr::Memory::Read);
            if(variant==9)memory->protect(0x1000,0x1000,wr::Memory::Write);
            if(variant==10)memory->protect(0x2000,0x1000,0);
            if(variant==12)memory->protect(0x1000,0x1000,0);
        }
        const auto got=run(false,op,actual,left,address),want=run(true,op,expected,right,address);
        const auto register_fault=run(false,op,register_cpu,register_memory,address,true);
        if(!(got==want && actual.fp.bytes==expected.fp.bytes && actual.r==expected.r && actual.flags==expected.flags && actual.defined_flags==expected.defined_flags)){
            std::cerr<<"wrapper difference op="<<op<<" rc="<<rc<<" mask="<<mask<<" variant="<<variant<<'\n';CHECK(false);
        }
        CHECK(register_fault==want && register_cpu.fp.bytes==expected.fp.bytes && register_cpu.r==expected.r && register_cpu.flags==expected.flags && register_cpu.defined_flags==expected.defined_flags);
        for(auto* memory:{&left,&right,&register_memory})memory->protect(0x1000,0x2000,3);
        std::array<std::uint8_t,0x2000> l,r,register_bytes;left.copy_out(0x1000,l);right.copy_out(0x1000,r);register_memory.copy_out(0x1000,register_bytes);CHECK(l==r && register_bytes==r);
        ++cases;if(got.raised)++faults;else ++completed;if(mask)++unmasked;else ++masked;
    }
    CHECK(faults>100 && completed>100);
    unsigned sequences{};
    for(unsigned precision:{0u,2u,3u})for(unsigned rounding=0;rounding<4;++rounding)for(unsigned variant=0;variant<32;++variant){
        wr::Cpu actual;actual.fp.put16(0,std::uint16_t(0x7f|(precision<<8)|(rounding<<10)));
        actual.fp.put16(2,std::uint16_t((variant%8)<<11));actual.fp.bytes[4]=variant&8?0xff:0;auto expected=actual;
        for(auto* memory:{&left,&right}){
            memory->protect(0x1000,0x2000,3);memory->store(0x1800,inputs[variant%8],32);memory->store(0x1804,inputs[(variant+3)%8],32);
            memory->protect(0x2000,4096,wr::Memory::Read);
        }
        const auto got=sequence(true,actual,left,variant),want=sequence(false,expected,right,variant);
        CHECK(got==want && actual.fp.bytes==expected.fp.bytes && actual.r==expected.r && actual.flags==expected.flags && actual.eip==expected.eip);
        std::array<std::uint8_t,0x2000> l,r;left.copy_out(0x1000,l);right.copy_out(0x1000,r);CHECK(l==r);++sequences;
    }
    std::ofstream report(argv[1]);report<<"{\"schema\":\"wa2.portable-fp-execution.v1\",\"cases\":"<<cases<<",\"matching_faults\":"<<faults
        <<",\"locked_byte_store_atomic_fault_and_code_epoch_cases\":"<<locked_stores
        <<",\"completed\":"<<completed<<",\"masked_cases\":"<<masked<<",\"unmasked_cases\":"<<unmasked
        <<",\"register_file_state_exception_memory_cases\":"<<cases
        <<",\"register_file_sequences_with_native_calls_and_faults\":"<<sequences
        <<",\"memory_and_register_state_compared\":true,\"source_executable_opened\":false,\"screen_access\":false,\"game_launched\":false,\"passed\":true}\n";
    std::cout<<"PASS floating execution: "<<cases<<" state/exception/memory cases.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
