// Bounded RAM operations only: compare bulk work with the existing precise
// string-operation semantics, including partially completed access faults.
#include "native_string.hpp"
#include "work_samples.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
namespace a=wa2::aot;
constexpr wr::U32 Source=0x3ff000,Destination=0x17ff000,High=0xfffff000,Partial=0x2200000;
constexpr unsigned Bytes=24576;
struct Request {
    unsigned kind{},width{32},address_width{32},repeat{1},segment{},count{63},value{0x75307530},fault_mode{};
    wr::U32 source{Source+3},destination{Destination+4093},flags{0xa93};
};
struct Snapshot {
    wr::Cpu cpu;std::vector<std::uint8_t> bytes;std::uint64_t budget{},epoch{},completed{};
    bool raised{};wr::FaultKind kind{};wr::U32 address{};std::string message;
};
Snapshot exercise(wr::Memory& memory,const Request& request,bool native,bool metered=false,std::uint64_t allowance=10000){
    Snapshot result{};auto& cpu=result.cpu;cpu.eip=0x12345678;
    for(unsigned n=0;n<cpu.r.size();++n)cpu.r[n]=0x13579bdu*n;
    for(unsigned n=0;n<cpu.fp.bytes.size();++n)cpu.fp.bytes[n]=std::uint8_t(n*17+9);
    cpu.flags=request.flags;cpu.defined_flags=0x691;cpu.fs_base=0x1000;cpu.gs_base=0x2000;
    cpu.fs_valid=request.segment!=1;cpu.gs_valid=true;
    cpu.r[wr::ESI]=request.source;cpu.r[wr::EDI]=request.destination;
    cpu.r[wr::ECX]=request.count;cpu.r[wr::EAX]=request.value;
    std::vector<std::uint8_t> pattern(Bytes);
    for(unsigned n=0;n<Bytes;++n)pattern[n]=std::uint8_t(n*13+(n>>7));
    memory.protect(Source,Bytes,3);memory.protect(Destination,Bytes,3);memory.protect(High,4096,3);memory.protect(Partial,5001,3);
    memory.copy_in(Source,pattern);std::fill(pattern.begin(),pattern.end(),0x9d);memory.copy_in(Destination,pattern);
    memory.copy_in(High,std::span(pattern).first(4096));memory.copy_in(Partial,std::span(pattern).first(5001));
    if(request.fault_mode==1)memory.protect(Destination+4096,4096,wr::Memory::Read);
    if(request.fault_mode==2)memory.protect(Source+4096,4096,0);
    if(request.fault_mode==3)memory.protect(Destination,Bytes,7);
    if(request.fault_mode==4)memory.protect(Destination,Bytes,wr::Memory::Write);
    a::FunctionWorkSamples samples(std::array<a::FunctionWorkSamples::Counter,0>{});a::FunctionWorkScope counting(&samples);
    result.budget=allowance;const auto epoch=memory.code_epoch();
    try{
        auto scope=memory.execution_scope();
        if(native){
            if(metered)wr::aot_string_op<true>(cpu,memory,request.kind,request.width,request.address_width,request.repeat,request.segment,result.budget);
            else wr::aot_string_op<false>(cpu,memory,request.kind,request.width,request.address_width,request.repeat,request.segment,result.budget);
        }else if(metered)wr::string_op<true>(cpu,memory,request.kind,request.width,request.address_width,request.repeat,request.segment,result.budget);
        else wr::string_op<false>(cpu,memory,request.kind,request.width,request.address_width,request.repeat,request.segment,result.budget);
    }catch(const wr::GuestFault& error){result.raised=true;result.kind=error.kind;result.address=error.address;result.message=error.what();}
    result.epoch=memory.code_epoch()-epoch;result.completed=samples.string_bulk_completed;
    memory.protect(Source,Bytes,3);memory.protect(Destination,Bytes,3);memory.protect(High,4096,3);memory.protect(Partial,5001,3);
    result.bytes.resize(Bytes*2+4096+5001);
    memory.copy_out(Source,std::span(result.bytes).first(Bytes));memory.copy_out(Destination,std::span(result.bytes).subspan(Bytes,Bytes));
    memory.copy_out(High,std::span(result.bytes).subspan(Bytes*2,4096));memory.copy_out(Partial,std::span(result.bytes).last(5001));return result;
}
void equal(const Snapshot& reference,const Snapshot& result){
    const auto& x=reference.cpu;const auto& y=result.cpu;
    CHECK(x.r==y.r && x.eip==y.eip && x.flags==y.flags && x.defined_flags==y.defined_flags && x.fp.bytes==y.fp.bytes);
    CHECK(x.selectors==y.selectors && x.fs_base==y.fs_base && x.gs_base==y.gs_base && x.fs_valid==y.fs_valid && x.gs_valid==y.gs_valid);
    CHECK(reference.bytes==result.bytes && reference.raised==result.raised && reference.kind==result.kind && reference.address==result.address);
    CHECK(reference.message==result.message && reference.budget==result.budget && reference.epoch==result.epoch);
}
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{},admitted{},faults{};
    for(bool linear:{false,true}){
        wr::Memory memory;if(linear)CHECK(memory.enable_linear_memory());
        memory.map(Source,Bytes,3);memory.map(Destination,Bytes,3);memory.map(High,4096,3);memory.map(Partial,5001,3);
        const auto check=[&](Request request,bool metered=false,unsigned allowance=10000){
            const auto old=exercise(memory,request,false,metered,allowance),now=exercise(memory,request,true,metered,allowance);
            equal(old,now);++cases;admitted+=unsigned(now.completed);faults+=now.raised;
            return now;
        };
        for(unsigned width:{8,16,32})for(unsigned kind:{0,1})for(unsigned seed=0;seed<24;++seed){
            Request r;r.width=width;r.kind=kind;r.repeat=1+seed%2;r.count=std::array{0u,1u,63u,1025u}[seed%4];
            r.source=Source+std::array{0u,1u,4093u,4095u}[seed%4];r.destination=Destination+std::array{4095u,4093u,1u,0u}[seed%4];
            r.value=std::array{0u,0xababababu,0x75307530u,0xa1b2c3d4u}[seed/6];
            const auto now=check(r);CHECK(!now.raised);CHECK(r.count==0 || now.completed==1);
        }
        for(unsigned kind:{0,1})for(unsigned width:{8,16,32}){
            Request r;r.kind=kind;r.width=width;r.count=100;r.source=Source+4000;r.destination=Destination+4000;
            for(unsigned mode:{1,2,3,4}){r.fault_mode=mode;check(r);}
            r.fault_mode=0;r.flags|=wr::DF;r.source+=300;r.destination+=300;check(r);r.flags&=~wr::DF;
            r.repeat=0;check(r);r.repeat=1;
            r.source=Source+4093;r.destination=r.source;check(r);
            for(int delta:{-3,-1,1,3}){r.destination=wr::U32(r.source+delta);check(r);}
            r.source=High+4088;r.destination=Destination+3;r.count=4;check(r);
            r.source=Source+3;r.destination=High+4088;check(r);
            r.destination=High+4096-width/8*2;r.count=2;CHECK(check(r).completed==1);
            r.count=200;r.destination=Partial+4900;check(r);
            r.destination=Destination+4093;r.source=Source+3;r.count=100;
            r.segment=1;CHECK(check(r).raised);r.segment=2;check(r);r.segment=0;
            r.address_width=16;check(r);r.address_width=32;
            for(unsigned allowance:{0,1,2,99,100,101})check(r,true,allowance);
        }
        // Non-MOVS/STOS instructions always retain their flag-setting semantics.
        for(unsigned kind:{2,3,4}){Request r;r.kind=kind;r.count=71;check(r);}
        for(unsigned reason=0;reason<8;++reason){
            Request reset;reset.count=0;exercise(memory,reset,false);
            wr::Cpu cpu;cpu.eip=0x47e798;cpu.r[wr::ECX]=1;cpu.r[wr::EDI]=Destination;
            if(reason==0)cpu.r[wr::ECX]=0;
            if(reason==1)cpu.r[wr::ECX]=8193;
            if(reason==2)++cpu.r[wr::EDI];
            if(reason==3)cpu.fp.put64(160,1);
            if(reason==4)cpu.fp.put64(168,1);
            if(reason==5)cpu.fp.put32(24,0x10000|0x1f80);
            if(reason==6){cpu.r[wr::ECX]=40;memory.protect(Destination+4096,4096,wr::Memory::Read);}
            if(reason==7)cpu.r[wr::EDI]=Partial+4992;
            const auto before=cpu;std::vector<std::uint8_t> data(Bytes),after(Bytes);memory.copy_out(Destination,data);
            CHECK(!a::native_vector_clear(cpu,memory));CHECK(cpu.r==before.r && cpu.flags==before.flags && cpu.defined_flags==before.defined_flags && cpu.eip==before.eip && cpu.fp.bytes==before.fp.bytes);
            memory.copy_out(Destination,after);CHECK(data==after);++cases;
        }
    }
    wr::Memory memory;CHECK(memory.enable_linear_memory());constexpr wr::U32 Buffer=0x2600000;memory.map(Buffer,262144,3);
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.native-string-work.v1\",\"exact_cpu_fp_ram_and_fault_cases\":"<<cases
        <<",\"admitted_bulk_cases\":"<<admitted<<",\"fault_cases\":"<<faults<<",\"components\":[";
    unsigned number{};
    for(const auto [bytes,value]:{std::pair{256u,0u},{8192u,0x75307530u},{192000u,0u}}){
        const unsigned repetitions=bytes==256?256:bytes==8192?128:4;std::array<std::array<std::uint64_t,2>,8> times{};
        std::vector<std::uint8_t> expected(bytes),actual(bytes);
        for(unsigned sample=0;sample<times.size();++sample)for(unsigned order=0;order<2;++order){
            const unsigned native=(sample+order)&1;wr::Cpu cpu;cpu.eip=0x12345678;cpu.r[wr::EAX]=value;std::uint64_t budget=0;
            auto scope=memory.execution_scope();const auto start=std::chrono::steady_clock::now();
            for(unsigned repeat=0;repeat<repetitions;++repeat){
                cpu.r[wr::ECX]=bytes/4;cpu.r[wr::EDI]=Buffer;
                if(native)wr::aot_string_op<false>(cpu,memory,1,32,32,1,0,budget);
                else wr::string_op<false>(cpu,memory,1,32,32,1,0,budget);
            }
            times[sample][native]=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
            CHECK(cpu.r[wr::ECX]==0 && cpu.r[wr::EDI]==Buffer+bytes && cpu.r[wr::EAX]==value);
            memory.copy_out(Buffer,actual);for(unsigned n=0;n<bytes;++n)expected[n]=std::uint8_t(value>>(8*(n&3)));CHECK(actual==expected);
        }
        if(number++)out<<',';out<<"{\"bytes\":"<<bytes<<",\"repetitions\":"<<repetitions<<",\"paired_ns\":[";
        for(unsigned n=0;n<times.size();++n){if(n)out<<',';out<<'['<<times[n][0]<<','<<times[n][1]<<']';}out<<"]}";
    }
    out<<"],\"mapped_and_linear_memory\":true,\"code_invalidation_preserved\":true,\"game_launched\":false,\"screen_access\":false,\"device_opened\":false,\"passed\":true}\n";
    std::cout<<"PASS "<<cases<<" exact bulk string-operation snapshots, "<<admitted<<" admitted and "<<faults<<" precise faults.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
