// Bounded resource-buffer operations in owned RAM. No game or device startup.
#include "functions.hpp"
#include "native_string.hpp"
#include "work_samples.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
namespace a=wa2::aot;
using wr::U32;
constexpr U32 Source=0x1200000,Destination=0x3200000,Stack=0x5200000,Return=0xf0001234;
constexpr unsigned MiB=1024*1024,Capacity=8*MiB+8192;
bool same(const wr::Cpu& x,const wr::Cpu& y){return x.r==y.r && x.eip==y.eip && x.flags==y.flags && x.defined_flags==y.defined_flags &&
    x.fp.bytes==y.fp.bytes && x.selectors==y.selectors && x.fs_base==y.fs_base && x.gs_base==y.gs_base && x.fs_valid==y.fs_valid && x.gs_valid==y.gs_valid;}
struct Result {wr::Cpu cpu;std::vector<std::uint8_t> bytes;bool fault{};wr::FaultKind kind{};U32 address{};std::string message;std::uint64_t epoch{},attempts{},completed{};};
struct Fixture {
    wr::Memory memory;a::Program program{memory};std::vector<std::uint8_t> input=std::vector<std::uint8_t>(Capacity),poison=std::vector<std::uint8_t>(Capacity,0x9d);
    explicit Fixture(bool linear){
        if(linear)CHECK(memory.enable_linear_memory());
        memory.map(0x401000,0xa0000,3);
        for(const auto& f:a::functions())for(const auto& span:f.code)memory.copy_in(span.address,span.bytes);
        memory.protect(0x401000,0xa0000,5);memory.map(Source,Capacity,3);memory.map(Destination,Capacity,3);memory.map(Stack,8192,3);
        for(unsigned n=0;n<input.size();++n)input[n]=std::uint8_t(n*37+(n>>8)*17);
    }
    void reset(){memory.protect(Source,Capacity,3);memory.protect(Destination,Capacity,3);memory.copy_in(Source,input);memory.copy_in(Destination,poison);}
    wr::Cpu state(){wr::Cpu s;for(unsigned n=0;n<s.r.size();++n)s.r[n]=0x13579bdu*(n+1);
        for(unsigned n=0;n<s.fp.bytes.size();++n)s.fp.bytes[n]=std::uint8_t(n*29+7);
        s.fp.put32(24,0x1f80);s.flags=0xa93;s.defined_flags=0x691;s.selectors={1,2,3,4,5,6};s.fs_base=0x12345678;s.gs_base=0xabcdef01;s.fs_valid=true;s.gs_valid=false;return s;}
    template<class Run>Result capture(wr::Cpu cpu,unsigned bytes,Run run){
        a::FunctionWorkSamples work(a::functions());a::FunctionWorkScope counting(&work);
        const auto epoch=memory.code_epoch();Result result;
        try{run(cpu);}catch(const wr::GuestFault& error){result.fault=true;result.kind=error.kind;result.address=error.address;result.message=error.what();}
        result.epoch=memory.code_epoch()-epoch;result.cpu=cpu;result.attempts=work.vector_clear_attempts;result.completed=work.vector_clear_completed;
        memory.protect(Destination,Capacity,3);result.bytes.resize(bytes+128);memory.copy_out(Destination,result.bytes);return result;
    }
    Result string(bool native,unsigned kind,unsigned width,bool backwards,unsigned bytes,int fault=0){
        reset();auto cpu=state();const auto step=width/8;cpu.eip=0x47f000;cpu.r[wr::EAX]=0x75307530;cpu.r[wr::ECX]=bytes/step;
        cpu.r[wr::ESI]=Source+64+(backwards?bytes-step:0);cpu.r[wr::EDI]=Destination+64+(backwards?bytes-step:0);if(backwards)cpu.flags|=wr::DF;
        if(fault)memory.protect(Destination+(backwards?0:bytes/4096*4096),4096,wr::Memory::Read);
        return capture(cpu,bytes,[&](auto& s){auto scope=memory.execution_scope();std::uint64_t budget{};
            if(native)wr::aot_string_op<false>(s,memory,kind,width,32,1,0,budget);else wr::string_op<false>(s,memory,kind,width,32,1,0,budget);});
    }
    wr::Cpu clear_state(unsigned bytes,unsigned offset=0){
        auto cpu=state();cpu.eip=0x47e78e;cpu.r[wr::ECX]=bytes/128;cpu.r[wr::EDI]=Destination+64+offset;cpu.r[wr::EBP]=Stack+4096;cpu.r[wr::ESP]=Stack+4092;
        cpu.fp.put64(160,0);cpu.fp.put64(168,0);memory.store(Stack+4092,0xaabbccdd,32);memory.store(Stack+4096,0x99887766,32);memory.store(Stack+4100,Return,32);return cpu;
    }
    Result clear(bool native,unsigned bytes,unsigned problem=0){
        reset();auto cpu=clear_state(bytes,problem==1?1:0);
        if(problem==2)memory.protect(Destination+4096,4096,wr::Memory::Read);
        if(problem==3)cpu.fp.put32(24,0x10000|0x1f80);
        if(problem==4)cpu.fp.put64(160,0x123456789);
        return capture(cpu,bytes,[&](auto& s){
            if(native)CHECK(program.invoke_unmetered(s));else{std::uint64_t budget=std::uint64_t(bytes/128)*11+16;CHECK(program.invoke(s,budget));}
        });
    }
};
void equal(const Result& x,const Result& y){
    if(!same(x.cpu,y.cpu)){
        for(unsigned n=0;n<8;++n)if(x.cpu.r[n]!=y.cpu.r[n])std::cerr<<"register "<<n<<": "<<x.cpu.r[n]<<" / "<<y.cpu.r[n]<<'\n';
        if(x.cpu.eip!=y.cpu.eip)std::cerr<<"eip "<<x.cpu.eip<<" / "<<y.cpu.eip<<'\n';
        if(x.cpu.flags!=y.cpu.flags || x.cpu.defined_flags!=y.cpu.defined_flags)std::cerr<<"flags "<<x.cpu.flags<<","<<x.cpu.defined_flags<<" / "<<y.cpu.flags<<","<<y.cpu.defined_flags<<'\n';
        for(unsigned n=0;n<x.cpu.fp.bytes.size();++n)if(x.cpu.fp.bytes[n]!=y.cpu.fp.bytes[n])std::cerr<<"fp "<<n<<": "<<unsigned(x.cpu.fp.bytes[n])<<" / "<<unsigned(y.cpu.fp.bytes[n])<<'\n';
    }
    CHECK(same(x.cpu,y.cpu));CHECK(x.bytes==y.bytes);CHECK(x.fault==y.fault && x.kind==y.kind && x.address==y.address && x.message==y.message && x.epoch==y.epoch);
}
int main(int argc,char** argv){std::string phase="arguments";try{
    CHECK(argc==2 || (argc==3 && std::string(argv[2])=="--bench"));const bool bench=argc==3;
    unsigned strings{},faults{},clears{},declines{},single_attempts{};
    if(!bench)for(bool linear:{false,true}){
        phase="fixture "+std::to_string(linear);Fixture f(linear);
        for(bool backwards:{false,true})for(unsigned kind:{0u,1u})for(unsigned width:{8u,32u}){
            phase="string "+std::to_string(linear)+" "+std::to_string(backwards)+" "+std::to_string(kind)+" "+std::to_string(width);
            const auto expected=f.string(false,kind,width,backwards,MiB+32);const auto result=f.string(true,kind,width,backwards,MiB+32);equal(expected,result);CHECK(!result.fault);++strings;
            const auto before_fault=f.string(false,kind,width,backwards,MiB+32,1);const auto after_fault=f.string(true,kind,width,backwards,MiB+32,1);equal(before_fault,after_fault);CHECK(after_fault.fault);++faults;
        }
        for(unsigned bytes:{128u,256u,4224u,131200u,MiB+128}){
            phase="clear "+std::to_string(linear)+" "+std::to_string(bytes);
            const auto before=f.clear(false,bytes),after=f.clear(true,bytes);equal(before,after);CHECK(after.cpu.eip==Return && after.attempts==1 && after.completed==1);++clears;
        }
        for(unsigned problem=1;problem<=4;++problem){
            phase="clear reject "+std::to_string(linear)+" "+std::to_string(problem);
            const auto before=f.clear(false,8192,problem),after=f.clear(true,8192,problem);equal(before,after);CHECK(after.completed==0 && after.attempts==1);++declines;++single_attempts;
            if(problem!=4)CHECK(after.fault);
        }
        f.reset();
        for(unsigned reason=0;reason<4;++reason){auto cpu=f.state();cpu.r[wr::ECX]=reason==0?64u*MiB+1:100;cpu.r[wr::EDI]=Destination;cpu.r[wr::ESI]=Source;
            if(reason==1){cpu.flags|=wr::DF;cpu.r[wr::EDI]=2;}
            if(reason==2){cpu.flags|=wr::DF;cpu.r[wr::ESI]=0xffffffff;}
            if(reason==3)cpu.r[wr::EDI]=Source+1;
            const auto before=cpu;CHECK(!a::try_bulk_string(cpu,f.memory,0,reason==0?8:32,32,1,0));CHECK(same(cpu,before));++declines;
        }
    }
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.large-native-memory.v1\",\"large_string_cases\":"<<strings<<",\"precise_partial_fault_cases\":"<<faults
        <<",\"integrated_clear_cases\":"<<clears<<",\"unchanged_declines\":"<<declines<<",\"single_clear_fallback_attempts\":"<<single_attempts<<",\"timings\":[";
    if(bench){Fixture f(true);constexpr unsigned bytes=4u*MiB,repetitions=2;
        for(unsigned mode=0;mode<4;++mode){f.reset();auto cpu=mode==3?f.clear_state(bytes):f.state();const auto initial=cpu;std::uint64_t budget{};
            auto scope=f.memory.execution_scope();const auto begin=std::chrono::steady_clock::now();
            for(unsigned n=0;n<repetitions;++n){cpu=initial;
                if(mode==3)CHECK(f.program.invoke_unmetered(cpu));
                else{cpu.eip=0x47f000;cpu.r[wr::ECX]=bytes/4;cpu.r[wr::EAX]=0x75307530;cpu.r[wr::ESI]=Source+64+(mode==1?bytes-4:0);cpu.r[wr::EDI]=Destination+64+(mode==1?bytes-4:0);if(mode==1)cpu.flags|=wr::DF;
                    wr::aot_string_op<false>(cpu,f.memory,mode==2?1:0,32,32,1,0,budget);}
            }
            const auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-begin).count();
            std::vector<std::uint8_t> actual(bytes+128);f.memory.copy_out(Destination,actual);
            for(unsigned n=0;n<actual.size();++n){const auto wanted=n<64 || n>=bytes+64?0x9du:mode<2?f.input[n]:mode==2?unsigned((0x75307530u>>(8*((n-64)&3)))&255):0u;CHECK(actual[n]==wanted);}
            if(mode)out<<',';out<<"{\"operation\":\""<<std::array{"forward_copy","backward_copy","word_fill","compiled_vector_clear"}[mode]<<"\",\"bytes\":"<<bytes<<",\"repetitions\":"<<repetitions<<",\"ns\":"<<elapsed<<",\"registers\":[";
            for(unsigned n=0;n<cpu.r.size();++n){if(n)out<<',';out<<cpu.r[n];}out<<"],\"eip\":"<<cpu.eip<<",\"flags\":"<<cpu.flags<<",\"defined_flags\":"<<cpu.defined_flags<<",\"fp\":[";
            for(unsigned n=0;n<cpu.fp.bytes.size();++n){if(n)out<<',';out<<unsigned(cpu.fp.bytes[n]);}out<<"],\"all_output_bytes_checked\":true}";
        }
    }
    out<<"],\"mapped_and_linear_memory\":true,\"game_launched\":false,\"screen_access\":false,\"device_opened\":false,\"passed\":true}\n";
    std::cout<<"PASS resource buffer operations: "<<strings<<" large copies/fills, "<<faults<<" precise partial faults, "<<clears<<" compiled clear calls.\n";return 0;
}catch(const std::exception& error){std::cerr<<phase<<": "<<error.what()<<'\n';return 1;}}
