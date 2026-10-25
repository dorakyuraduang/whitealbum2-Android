#include "process.hpp"
#include <array>
#include <bit>
#include <fstream>
#include <iostream>
#include <limits>

#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed at ")+std::to_string(__LINE__)+": "+#x);}while(false)
using wr::U32;
constexpr U32 Return=0xf1234560;
int main(int argc,char** argv){try{
    CHECK(argc==2);
    unsigned calls{},accessors{},destructors{},formats{},positives{},filters{},resumes{},guards{},registrations{};
    for(bool linear:{false,true}){
        wr::ProcessOptions options;options.prefer_linear_memory=linear;
        wa2::aot::Runtime runtime(options);runtime.load();auto& p=runtime.process();auto& m=p.memory;
        CHECK(!p.gui() && !p.directdraw());wa2::aot::Program program(m);
        const auto work=p.allocate_bytes(8192),object=work+512,locale=work+1024;
        auto store64=[&](U32 at,std::uint64_t value){m.store(at,U32(value),32);m.store(at+4,U32(value>>32),32);};
        auto load64=[&](U32 at){return std::uint64_t(m.load(at,32))|(std::uint64_t(m.load(at+4,32))<<32);};
        auto call=[&](U32 entry,std::initializer_list<U32> args,U32 self=0,bool callee_cleanup=false){
            auto cpu=p.cpu;cpu.eip=entry;cpu.r[wr::ECX]=self;
            cpu.r[wr::EBX]=0x12345678;cpu.r[wr::ESI]=0x23456789;cpu.r[wr::EDI]=0x3456789a;
            const auto before=cpu;
            for(auto it=args.end();it!=args.begin();)wr::push(cpu,m,*--it);
            wr::push(cpu,m,Return);std::uint64_t budget=20000;
            while(cpu.eip!=Return){
                if(!program.invoke(cpu,budget))throw std::runtime_error("unregistered callback dependency: "+std::to_string(cpu.eip));
            }
            CHECK(cpu.r[wr::EBX]==before.r[wr::EBX] && cpu.r[wr::ESI]==before.r[wr::ESI] && cpu.r[wr::EDI]==before.r[wr::EDI]);
            CHECK(cpu.r[wr::EBP]==before.r[wr::EBP]);
            CHECK(cpu.r[wr::ESP]==before.r[wr::ESP]-(callee_cleanup?0:U32(args.size()*4)));++calls;
            return cpu.r[wr::EAX];
        };
        for(U32 table:{0x4a1e5cu,0x4a288cu,0x4a2890u,0x4a81f8u,0x4a8204u}){
            CHECK(wa2::aot::find(U32(m.load(table,32))));++registrations;
        }
        // what(): select inline storage vs the separately owned string at
        // the exact source capacity boundary. Invoke through the real vtable.
        for(U32 capacity:{0u,15u,16u,255u,0xffffffffu}){
            m.store(object+0x10,work+64,32);m.store(object+0x24,capacity,32);
            CHECK(call(U32(m.load(0x4a2890,32)),{},object)==(capacity<16?object+0x10:work+64));++accessors;
        }
        // Nondeleting destructor paths must preserve the object, clean the
        // inline string, and retain the original RET 4 calling convention.
        for(U32 entry:{0x402360u,0x414650u,0x475220u,0x475247u})for(U32 flag:{0u,2u}){
            std::array<std::uint8_t,64> zero{};m.copy_in(object,zero);
            m.store(object+0x24,15,32);m.store(object+0x20,7,32);m.store(object+0x10,'x',8);
            CHECK(call(entry,{flag},object,true)==object && m.load(object,32)==0x4a81cc);
            if(entry==0x414650)CHECK(m.load(object+0x24,32)==15 && m.load(object+0x20,32)==0 && m.load(object+0x10,8)==0);
            ++destructors;
        }
        // The source's "positive" predicate includes both signed zeros.
        // Exercise FCOMP memory and x87 status transfer with infinities,
        // subnormal and unordered (NaN) input as well.
        for(double value:{-2.5,-0.0,0.0,2.5,std::numeric_limits<double>::denorm_min(),
                -std::numeric_limits<double>::infinity(),std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}){
            store64(work,std::bit_cast<std::uint64_t>(value));
            CHECK(call(0x47c0a6,{work})==U32(value>=0));++positives;
        }
        m.store(locale,m.load(0x4d1cb0,32),32);m.store(locale+4,m.load(0x4d1ad0,32),32);
        CHECK(m.load(locale,32));
        auto string_case=[&](U32 entry,const char* input,const char* expected){
            std::array<std::uint8_t,128> sentinel;sentinel.fill(0xa5);m.copy_in(work,sentinel);
            m.copy_in(work,{reinterpret_cast<const std::uint8_t*>(input),std::char_traits<char>::length(input)+1});
            call(entry,{work,locale});CHECK(p.read_string(work)==expected);
            CHECK(m.load(work+127,8)==0xa5);++formats;
        };
        for(auto pair:{std::pair{"12","12."},std::pair{"-12","-12."},std::pair{"1e+03","1.e+03"}})
            string_case(0x47bfb1,pair.first,pair.second);
        for(auto pair:{std::pair{"12.3400","12.34"},std::pair{"12.000","12"},std::pair{"1.2000E+03","1.2E+03"},
                std::pair{"100","100"},std::pair{"0.000","0"},std::pair{"",""}})
            string_case(0x47c024,pair.first,pair.second);
        for(auto pair:{std::pair{"12.5",12.5},std::pair{"-0.125",-0.125},std::pair{"1.25e2",125.0},std::pair{"0",0.0}}){
            m.copy_in(work,{reinterpret_cast<const std::uint8_t*>(pair.first),std::char_traits<char>::length(pair.first)+1});
            for(U32 wide:{0u,1u}){
                store64(work+256,0xa5a5a5a5a5a5a5a5ull);
                call(0x47c0c2,{wide,work+256,work,locale});
                if(wide)CHECK(load64(work+256)==std::bit_cast<std::uint64_t>(pair.second));
                else CHECK(m.load(work+256,32)==std::bit_cast<U32>(float(pair.second)) && m.load(work+260,32)==0xa5a5a5a5);
                ++formats;
            }
        }
        // The filter must decline unrelated exceptions; matching C++ fatal
        // exceptions are deliberately not raised by a headless component test.
        for(auto values:{std::array<U32,3>{0,3,0x19930520},std::array<U32,3>{0xe06d7363,2,0x19930520},std::array<U32,3>{0xe06d7363,3,0}}){
            m.store(work,object,32);m.store(object,values[0],32);m.store(object+0x10,values[1],32);m.store(object+0x14,values[2],32);
            CHECK(call(0x487091,{work},0,true)==0);++filters;
        }
        // A catch funclet returns this previously omitted resumption prefix.
        // Execute only its two restore instructions, with a strict budget.
        {
            auto cpu=p.cpu;cpu.eip=0x41417f;cpu.r[wr::EBP]=object+64;
            m.store(cpu.r[wr::EBP]-0x14,0x12345678,32);m.store(cpu.r[wr::EBP]-0x18,0x23456789,32);
            std::uint64_t budget=2;bool stopped=false;
            try{program.invoke(cpu,budget);}catch(const wr::GuestFault& fault){stopped=fault.kind==wr::FaultKind::budget && fault.address==0x414185;}
            CHECK(stopped && cpu.r[wr::EDI]==0x12345678 && cpu.r[wr::ESI]==0x23456789 && budget==0);++resumes;
        }
        // Newly restored code obeys the same immutable-code checks and does
        // not turn unknown addresses into successful no-op callbacks.
        for(bool changed:{false,true}){
            const U32 entry=0x414640;const auto byte=m.load(entry,8);
            m.protect(0x414000,4096,wr::Memory::Read|wr::Memory::Write);
            if(changed){m.store(entry,byte^1,8);m.protect(0x414000,4096,wr::Memory::Read|wr::Memory::Execute);}
            auto cpu=p.cpu;cpu.eip=entry;cpu.r[wr::ECX]=object;const auto before=cpu;std::uint64_t budget=32;bool rejected=false;
            try{program.invoke(cpu,budget);}catch(const wr::GuestFault& fault){rejected=fault.kind==wr::FaultKind::unsupported;}
            CHECK(rejected && cpu.r==before.r && cpu.eip==before.eip);++guards;
            m.protect(0x414000,4096,wr::Memory::Read|wr::Memory::Write);m.store(entry,byte,8);
            m.protect(0x414000,4096,wr::Memory::Read|wr::Memory::Execute);
        }
        auto unknown=p.cpu;unknown.eip=0x414641;const auto before=unknown;
        CHECK(!program.invoke_unmetered(unknown) && unknown.r==before.r && unknown.eip==before.eip);++guards;
        CHECK(p.executed_instructions==0);
    }
    std::ofstream report(argv[1]);report<<"{\"schema\":\"wa2.cached-callback-memory.v1\",\"memory_modes\":2,\"completed_calls\":"<<calls
        <<",\"accessor_cases\":"<<accessors<<",\"destructor_cases\":"<<destructors<<",\"format_cases\":"<<formats
        <<",\"positive_cases\":"<<positives<<",\"filter_cases\":"<<filters<<",\"continuation_cases\":"<<resumes
        <<",\"guard_cases\":"<<guards<<",\"vtable_entries\":"<<registrations
        <<",\"source_executable_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";
    CHECK(report.good());std::cout<<"PASS recovered callbacks: "<<calls<<" synthetic calls.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
