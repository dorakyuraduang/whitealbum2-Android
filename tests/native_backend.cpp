// Register-resident x64 backend versus unchanged generated instruction bodies.
#include <winrecomp/compiled.hpp>
#include <winrecomp/core.hpp>
#include "native_backend.hpp"
#include "linear_faults.hpp"
#include "native_state_apis.hpp"
#include "vertex_batch.hpp"
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <random>
#include <set>
using namespace wr;
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(false)
#define ORACLE(page) bool code_page_##page(wr::Cpu&,wr::Memory&,std::uint64_t&);
ORACLE(1032) ORACLE(1081) ORACLE(1083) ORACLE(1084) ORACLE(1085) ORACLE(1110) ORACLE(1124)
ORACLE(1129) ORACLE(1134) ORACLE(1136) ORACLE(1139) ORACLE(1140) ORACLE(1150) ORACLE(1183)
ORACLE(1148) ORACLE(1149)
ORACLE(1133) ORACLE(1141)
ORACLE(1090)
namespace {
constexpr U32 stack=0x07001000,stop_pc=0xfffffff0;
constexpr U32 code_pages[]={0x408000,0x439000,0x43b000,0x43c000,0x43d000,0x442000,0x456000,0x464000,0x469000,0x46d000,0x46e000,0x470000,0x473000,0x474000,0x475000,0x47c000,0x47d000,0x47e000,0x49f000};
constexpr std::pair<U32,unsigned> data_regions[]={{0x4c2000,4096},{0xab3000,16384},{0x07000000,16384},{0x20000000,65536}};
std::array<std::uint8_t,0xa0000> eligible{};
std::vector<std::uint8_t> image_bytes;
bool has_oracle(U32 pc){return std::find(std::begin(code_pages),std::end(code_pages),pc&~4095u)!=std::end(code_pages);}
bool selected(U32 pc){return pc>=0x400000 && pc<0x4a0000 && eligible[pc-0x400000];}
bool original(Cpu& cpu,Memory& memory,std::uint64_t& budget){
    switch(cpu.eip>>12){
    case 0x408:return code_page_1032(cpu,memory,budget);case 0x439:return code_page_1081(cpu,memory,budget);
    case 0x43b:return code_page_1083(cpu,memory,budget);case 0x43c:return code_page_1084(cpu,memory,budget);
    case 0x43d:return code_page_1085(cpu,memory,budget);case 0x456:return code_page_1110(cpu,memory,budget);
    case 0x464:return code_page_1124(cpu,memory,budget);case 0x469:return code_page_1129(cpu,memory,budget);
    case 0x46e:return code_page_1134(cpu,memory,budget);case 0x470:return code_page_1136(cpu,memory,budget);
    case 0x46d:return code_page_1133(cpu,memory,budget);case 0x475:return code_page_1141(cpu,memory,budget);
    case 0x442:return code_page_1090(cpu,memory,budget);
    case 0x473:return code_page_1139(cpu,memory,budget);case 0x474:return code_page_1140(cpu,memory,budget);
    case 0x47c:return code_page_1148(cpu,memory,budget);case 0x47d:return code_page_1149(cpu,memory,budget);
    case 0x47e:return code_page_1150(cpu,memory,budget);case 0x49f:return code_page_1183(cpu,memory,budget);
    default:return false;
    }
}
struct Case {
    Cpu cpu;std::uint64_t budget=1000;unsigned limit=192;
    U32 return_pc=stop_pc,readonly{},unmapped{},corrupt{};bool writable_code{};
    std::vector<std::pair<U32,U32>> words;
    std::vector<std::pair<U32,unsigned>> extra_regions;
    bool state_api{},revoke_api{};
    bool custom_viewport{};
    bool track_differences{};
    U32 api_address{0xf0000000};
    U32 api_kind{2},api_rows{8},api_columns{33},api_receiver{0x20000100};
};
struct Fixture {
    Memory memory;
    std::array<U32,512> api_values{},api_baseline{};std::uint64_t api_calls{},method_calls{},api_version{};
    std::array<U32,65536> live_texture_tokens{};
    std::shared_ptr<Wa2StateApiTable> state_apis;
    Fixture(const Image& image,const Case& c,bool linear=false){
        if(c.api_kind==5){api_values[0]=0x20000120;api_values[1]=c.custom_viewport?0:1;}
        live_texture_tokens[0x2001]=0x20010000;live_texture_tokens[0x2002]=0x20020000;
        api_baseline=api_values;
        if(linear)CHECK(memory.enable_linear_memory());
        for(auto page:code_pages){memory.map(page,4096,Memory::Read|Memory::Execute|(c.writable_code?Memory::Write:0));memory.initialize(page,image.code(page).first(4096));}
        for(auto [at,size]:data_regions){
            memory.map(at,size,Memory::Read|Memory::Write);std::vector<std::uint8_t> bytes(size);
            for(unsigned k=0;k<size;++k)bytes[k]=std::uint8_t((k*17+(k>>3)+at/4096)&255);
            memory.initialize(at,bytes);
        }
        memory.map(0x4a0000,0x20000,Memory::Read);memory.initialize(0x4a0000,std::span(image_bytes).subspan(0x4a0000-image.base,0x20000));
        memory.map(0x4d0000,0x2000,Memory::Read);memory.initialize(0x4d0000,std::span(image_bytes).subspan(0x4d0000-image.base,0x2000));
        for(auto [at,size]:c.extra_regions)memory.map(at,size,Memory::Read|Memory::Write);
        for(auto [at,value]:c.words)memory.store(at,value,32);
        if(c.corrupt)memory.initialize(c.corrupt,std::array<std::uint8_t,1>{0xcc});
        if(c.readonly)memory.protect(c.readonly&~4095u,4096,Memory::Read);
        if(c.unmapped)memory.decommit(c.unmapped&~4095u,4096);
        if(c.state_api && linear){
            state_apis=wa2_state_apis(memory,&api_calls);
            wa2_bind_state_api(*state_apis,c.api_address,c.api_receiver,Wa2StateApiKind(c.api_kind),api_values.data(),c.api_rows,c.api_columns,&method_calls,&api_version,
                c.api_kind==7?live_texture_tokens.data():c.track_differences?api_baseline.data():nullptr);
            if(c.revoke_api)wa2_unbind_state_receiver(*state_apis,c.api_receiver);
        }
    }
};
struct Result {Cpu cpu;std::uint64_t budget{},count{},batches{};std::vector<std::uint8_t> bytes;std::string fault;};
Result execute(const Image& image,const Case& c,bool native){
    Fixture fixture(image,c,native);auto& m=fixture.memory;Result result;auto cpu=c.cpu;auto budget=c.budget;
    try{
        auto execution=m.execution_scope();
        while(result.count<c.limit && cpu.eip!=c.return_pc && (selected(cpu.eip) || (c.state_api && cpu.eip==c.api_address))){
            if(native)if(const auto count=wa2_native_batch(cpu,m,budget,c.limit-unsigned(result.count),c.return_pc)){
                CHECK(count<=c.limit-result.count);result.count+=count;++result.batches;continue;
            }
            if(c.state_api && cpu.eip==c.api_address){
                tick(budget,cpu.eip);const auto sp=cpu.r[ESP];const auto ret=m.load(sp,32);
                const auto argument=[&](unsigned index){const auto at=std::uint64_t(sp)+4+index*4;
                    if(at+4>0x100000000ull)throw GuestFault(FaultKind::memory,sp,"API stack wraps");return m.load(U32(at),32);};
                const auto receiver=argument(0),first=argument(1),second=c.api_kind==4?0:argument(2);
                const auto value=c.api_kind==4?first:(c.api_kind==2 || c.api_kind==3)?argument(3):second;
                const auto cleanup=c.api_kind==4?12:(c.api_kind==2 || c.api_kind==3)?20:16;
                m.check(sp,cleanup,Memory::Read);++fixture.api_calls;
                if(receiver!=c.api_receiver)throw GuestFault(FaultKind::unsupported,cpu.eip,"stale interface");
                ++fixture.method_calls;U32 answer=0;
                if((c.api_kind==1 && first>=c.api_columns) || ((c.api_kind==2 || c.api_kind==3) && (first>=c.api_rows || second>=c.api_columns)) ||
                   (c.api_kind==5 && (first || !second)) || (c.api_kind>=6 && first>=c.api_columns))answer=0x8876086c;
                else{
                    if(c.api_kind==1 && value && (first==28 || first==52 || first==137))throw GuestFault(FaultKind::unsupported,cpu.eip,"unsupported render state");
                    if(c.api_kind>=5 && value==0xffffffffu)throw GuestFault(FaultKind::unsupported,cpu.eip,"stale resource");
                    if(c.api_kind==7 && value && (value>>16>=c.api_rows || fixture.live_texture_tokens[value>>16]!=value))throw GuestFault(FaultKind::unsupported,cpu.eip,"stale texture");
                    const auto index=c.api_kind==1 || c.api_kind>=6?first:c.api_kind>=4?0:first*c.api_columns+second;
                    auto& destination=fixture.api_values[index];
                    if(c.track_differences){fixture.api_version-=destination!=fixture.api_baseline[index];fixture.api_version+=value!=fixture.api_baseline[index];}
                    else if(destination!=value || (c.api_kind==5 && !fixture.api_values[1]))++fixture.api_version;
                    destination=value;
                    if(c.api_kind==5)fixture.api_values[1]=1;
                }
                cpu.r[EAX]=answer;cpu.r[ESP]+=cleanup;cpu.eip=ret;continue;
            }
            tick(budget,cpu.eip);CHECK(original(cpu,m,budget));++result.count;
        }
    }catch(const GuestFault& fault){result.fault=std::to_string(int(fault.kind))+":"+std::to_string(fault.address);}
    result.cpu=cpu;result.budget=budget;
    auto append=[&](U32 base,unsigned size){for(unsigned offset=0;offset<size;offset+=4096){if(base+offset==(c.unmapped&~4095u))continue;const auto at=result.bytes.size();result.bytes.resize(at+4096);m.copy_out(base+offset,std::span(result.bytes).subspan(at,4096));}};
    for(auto page:code_pages)append(page,4096);
    for(auto [base,size]:data_regions)append(base,size);
    for(auto [base,size]:c.extra_regions)append(base,size);
    if(c.state_api){const auto at=result.bytes.size();result.bytes.resize(at+sizeof(fixture.api_values)+24);
        std::memcpy(result.bytes.data()+at,fixture.api_values.data(),sizeof(fixture.api_values));
        std::memcpy(result.bytes.data()+at+sizeof(fixture.api_values),&fixture.api_calls,8);
        std::memcpy(result.bytes.data()+at+sizeof(fixture.api_values)+8,&fixture.method_calls,8);
        std::memcpy(result.bytes.data()+at+sizeof(fixture.api_values)+16,&fixture.api_version,8);}
    return result;
}
void equal(const Result& a,const Result& b,U32 entry){
    if(a.cpu.r!=b.cpu.r || a.cpu.eip!=b.cpu.eip || a.cpu.flags!=b.cpu.flags || a.cpu.defined_flags!=b.cpu.defined_flags ||
       a.cpu.fp.bytes!=b.cpu.fp.bytes || a.budget!=b.budget || a.count!=b.count || a.bytes!=b.bytes || a.fault!=b.fault){
        std::cerr<<"entry=0x"<<std::hex<<entry<<" pc="<<a.cpu.eip<<"/"<<b.cpu.eip<<" flags="<<a.cpu.flags<<"/"<<b.cpu.flags
            <<" defined="<<a.cpu.defined_flags<<"/"<<b.cpu.defined_flags<<std::dec<<" count="<<a.count<<"/"<<b.count
            <<" budget="<<a.budget<<"/"<<b.budget<<" fault="<<a.fault<<"/"<<b.fault<<'\n';
        for(unsigned k=0;k<8;++k)if(a.cpu.r[k]!=b.cpu.r[k])std::cerr<<"reg "<<k<<" "<<std::hex<<a.cpu.r[k]<<"/"<<b.cpu.r[k]<<std::dec<<'\n';
        for(unsigned k=0;k<512;++k)if(a.cpu.fp.bytes[k]!=b.cpu.fp.bytes[k]){std::cerr<<"FP byte "<<k<<" "<<unsigned(a.cpu.fp.bytes[k])<<"/"<<unsigned(b.cpu.fp.bytes[k])<<'\n';break;}
        throw std::runtime_error("native backend differs from original instruction bodies");
    }
    CHECK(a.cpu.selectors==b.cpu.selectors && a.cpu.fs_base==b.cpu.fs_base && a.cpu.gs_base==b.cpu.gs_base && a.cpu.fs_valid==b.cpu.fs_valid && a.cpu.gs_valid==b.cpu.gs_valid);
}
Case color_case(){
    Case c;c.cpu.eip=0x43b293;c.cpu.r={0,0,0,0,stack-8,0,0,0};c.return_pc=stop_pc;
    c.words={{stack,stop_pc},{stack+4,5},{stack+8,2},{stack+12,60},{stack+16,100},{stack+20,128},{stack+24,150},{stack+28,180},{stack+32,220}};
    return c;
}
Case transform_case(){
    Case c;c.cpu.eip=0x43bbaa;c.cpu.r={0,0,0,0,stack,0,0x20000200,0};
    c.cpu.fp.bytes[4]=1; // ST(0)=0, matching the preceding FLDZ boundary.
    return c;
}
struct Timing {double seconds{};Result final;};
Timing benchmark(const Image& image,const Case& c,unsigned backend,unsigned repeats){
    Fixture fixture(image,c,backend!=0);auto& m=fixture.memory;Timing timing;auto budget=std::uint64_t(1000000000);
    const auto start=std::chrono::steady_clock::now();
    for(unsigned repeat=0;repeat<repeats;++repeat){
        for(auto [at,value]:c.words)m.store(at,value,32);
        auto cpu=c.cpu;unsigned used=0;auto execution=m.execution_scope();
        while(used<c.limit && cpu.eip!=c.return_pc && (selected(cpu.eip) || (c.cpu.eip==0x43bde0 && cpu.eip>=0x43bde0 && cpu.eip<0x43c100))){
            if(backend==2)if(const auto n=wa2_vertex_batch(cpu,m,budget,c.limit-used,c.return_pc)){used+=n;continue;}
            if(backend==1)if(const auto n=wa2_native_batch(cpu,m,budget,c.limit-used,c.return_pc)){used+=n;continue;}
            tick(budget,cpu.eip);CHECK(original(cpu,m,budget));++used;
        }
        timing.final.cpu=cpu;timing.final.count+=used;
    }
    timing.seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();timing.final.budget=budget;
    for(auto [base,size]:data_regions){const auto at=timing.final.bytes.size();timing.final.bytes.resize(at+size);m.copy_out(base,std::span(timing.final.bytes).subspan(at,size));}
    return timing;
}
}
int main(int argc,char** argv){try{
    CHECK(argc==3);const auto image=Image::load(argv[1]);image_bytes=image.mapped();std::mt19937 random(0x64a07);
    for(auto instruction:wa2_native_instructions())if(has_oracle(instruction.pc))eligible[instruction.pc-0x400000]=1;
    unsigned cases=0,entries_tested=0;std::uint64_t batches=0;
    auto compare=[&](const Case& c){const auto a=execute(image,c,false),b=execute(image,c,true);equal(a,b,c.cpu.eip);++cases;batches+=b.batches;};
    for(auto entry:wa2_native_entries())if(has_oracle(entry))for(unsigned mode=0;mode<2;++mode){
        if(!mode)++entries_tested;
        Case c;c.cpu.eip=entry;c.cpu.flags=random();c.cpu.defined_flags=random();
        for(unsigned k=0;k<8;++k)c.cpu.r[k]=mode?(random()%2?random()%64:random()):0x20001000+k*0x200;
        c.cpu.r[ESP]=stack;c.budget=mode?random()%48:300;c.limit=mode?1+random()%64:192;
        for(auto& byte:c.cpu.fp.bytes)byte=std::uint8_t(random());
        c.cpu.fp.put16(0,0x37f);c.cpu.fp.put16(2,std::uint16_t(random()&0x7f3f));c.cpu.fp.put32(24,0x1f80);
        c.cpu.fp.put64(8,random());c.cpu.fp.put64(16,random());c.cpu.fp.put16(6,std::uint16_t(random()&0x7ff));
        compare(c);
    }
    for(unsigned variant=0;variant<1000;++variant){
        auto c=variant&1?color_case():transform_case();c.cpu.flags=random();c.cpu.defined_flags=random();
        c.limit=variant%3?192:random()%160;c.budget=variant%5?1000:random()%96;
        if(variant%13==0)c.readonly=stack;
        if(variant%17==0)c.unmapped=0x20000000;
        if(variant%19==0)c.corrupt=c.cpu.eip;
        if(variant%23==0)c.cpu.fp.put16(0,0x37e);
        if(variant%29==0)c.return_pc=c.cpu.eip+1;
        compare(c);
    }
    {auto c=color_case();c.budget=~std::uint64_t(0);compare(c);}
    {auto c=transform_case();c.budget=~std::uint64_t(0);compare(c);}
    for(const auto& region:wa2_native_forward_regions())if(has_oracle(region.pc))for(unsigned mode=0;mode<3;++mode){
        Case c;c.cpu.eip=region.pc;c.limit=512;c.budget=region.maximum+(mode==0?-1:mode==1?0:1);
        c.cpu.flags=random();c.cpu.defined_flags=random();
        for(unsigned k=0;k<8;++k)c.cpu.r[k]=0x20001000+k*0x200;
        c.cpu.r[ESP]=stack;c.words={{stack,stop_pc}};compare(c);
    }
    // Old codec-only division sites are deliberately no longer executable in
    // the release backend. Exercise the same signed-division behavior at two
    // live script/render sites, including different divisor registers.
    for(auto pc:{0x46962fu,0x469647u})CHECK(!selected(pc) && !wa2_native_entry(pc));
    for(auto [pc,divisor_register]:std::array{std::pair{0x408437u,ECX},std::pair{0x43d7c9u,EBX}})
        for(auto numerator:{0u,1u,0xffffffffu,0x7fffffffu,0x80000000u,1234567u,0xfffffffeu})
        for(auto divisor:{0u,1u,0xffffffffu,2u,0xfffffffeu,3u,0x80000000u,0x7fffffffu})for(unsigned mode=0;mode<3;++mode){
            CHECK(selected(pc) && wa2_native_entry(pc));Case c;c.cpu.eip=pc;c.cpu.r[ESP]=stack;c.limit=1;
            c.cpu.r[EAX]=numerator;c.cpu.r[EDX]=mode==1?0x12345678:(numerator&0x80000000u?0xffffffffu:0);c.cpu.r[divisor_register]=divisor;
            c.cpu.flags=random();c.cpu.defined_flags=random();if(mode==2)c.budget=0;compare(c);
            if(mode==0 && divisor && !(numerator==0x80000000u && divisor==0xffffffffu)){
                const auto result=execute(image,c,true);CHECK(result.batches==1 && result.count==1 && result.cpu.eip==pc+2);
            }
        }
    // The CRT cookie return and stack probe occur on every rendered frame.
    // RET's F3 prediction hint must not repeat, and swapping ESP must retain
    // the exact original stack, registers, flags, and fault prefix.
    for(auto pc:{0x47571cu,0x47574bu,0x475757u,0x4422a9u,0x408942u})CHECK(selected(pc));
    for(unsigned mode=0;mode<32;++mode){
        Case c;c.cpu.eip=0x47571c;c.cpu.r[ESP]=stack;c.cpu.r[ECX]=random();
        c.cpu.flags=random();c.cpu.defined_flags=random();c.words={{stack,stop_pc}};
        if(mode==1)c.budget=0;if(mode==2)c.unmapped=stack;if(mode==3)c.readonly=stack;
        compare(c);
        if(!mode){const auto result=execute(image,c,true);CHECK(result.batches==1 && result.count==1 && result.cpu.eip==stop_pc);}
    }
    for(auto allocation:{0u,1u,0x100u,0xffcu,0x1000u,0x2000u,0x2ffcu})for(unsigned mode=0;mode<8;++mode){
        Case c;c.cpu.eip=0x475730;c.cpu.r[ESP]=stack+0x2000;c.cpu.r[EAX]=allocation;c.cpu.r[ECX]=random();
        c.cpu.flags=random();c.cpu.defined_flags=random();c.words={{stack+0x2000,stop_pc}};
        if(mode==1)c.limit=7;if(mode==2)c.budget=7;if(mode==3)c.readonly=stack+0x2000;
        if(mode==4)c.unmapped=stack;if(mode==5)c.readonly=stack;
        if(mode==6)c.limit=1;if(mode==7)c.budget=0;
        compare(c);
        if(!mode){const auto result=execute(image,c,true);CHECK(result.batches==1 && result.cpu.eip==stop_pc && result.cpu.r[ESP]==stack+0x2004-allocation);}
    }
    // Capstone omits access metadata for TEST. Check read-only and split-page
    // reads, exact flags, zero budgets, and faults after the preceding MOV.
    for(auto pc:{0x4422a3u,0x408942u})for(unsigned mode=0;mode<16;++mode){
        Case c;c.cpu.eip=pc;c.cpu.r[ESP]=stack;c.cpu.r[EAX]=mode&1?0x20000ffcu:0x20000100;
        c.cpu.r[EBX]=random();c.cpu.flags=random();c.cpu.defined_flags=random();
        c.extra_regions={{0x522000,4096}};c.words={{0x4c22b0,random()},{0x522944,random()}};
        c.limit=pc==0x4422a3?3:2;
        if(mode==2)c.readonly=0x20001000;if(mode==3)c.unmapped=0x20001000;
        if(mode==4)c.readonly=0x522000;if(mode==5)c.unmapped=0x522000;
        if(mode==6)c.budget=0;if(mode==7)c.budget=1;
        compare(c);
        if(!mode){const auto result=execute(image,c,true);CHECK(result.batches==1 && result.count==c.limit);}
    }
    // The game switches x87 precision to 24 bits; cover all precision and
    // rounding encodings rather than assuming the default 64-bit precision.
    for(unsigned precision=0;precision<4;++precision)for(unsigned rounding=0;rounding<4;++rounding){
        auto c=transform_case();c.cpu.fp.put16(0,std::uint16_t(0x7f|(precision<<8)|(rounding<<10)));compare(c);
    }
    for(auto [mantissa,exponent]:std::array<std::pair<std::uint64_t,std::uint16_t>,9>{{
        {0,0},{0,0x8000},{0xc000000000000000ull,0x3fff},{0xc000000000000000ull,0xbfff},
        {0xfffffffe00000000ull,0x401d},{0x8000000000000000ull,0x401e},
        {0x8000000000000000ull,0x7fff},{0xc000000000000000ull,0x7fff},{1,0}}})
        for(unsigned rounding=0;rounding<4;++rounding)for(auto cw:{0x3fu,0x7fu,0x17fu,0x37fu,0x1f7fu}){
            Case c;c.cpu.eip=0x474f00;c.cpu.r[ESP]=stack;c.cpu.r[EBP]=0x20000100;
            c.extra_regions={{0xbc8000,4096}};c.words={{stack,stop_pc},{0xbc8310,1}};
            c.cpu.fp.bytes[4]=1;c.cpu.fp.put64(32,mantissa);c.cpu.fp.put16(40,exponent);
            c.cpu.fp.put16(0,std::uint16_t(cw));c.cpu.fp.put32(24,0x1fbf|(rounding<<13));compare(c);
        }
    {Case c;c.cpu.eip=0x474fa9;c.cpu.r[ESP]=stack-128;c.cpu.r[EBP]=stack;
        c.words={{stack,0x20000100},{stack+4,stop_pc}};compare(c);c.unmapped=stack;compare(c);}
    // Control-word transitions occur repeatedly in the game's vertex path.
    // Cover reserved bits, every mask/precision/rounding encoding, a pending
    // status word, and the exact guest memory fault before the control load.
    for(unsigned cw=0;cw<0x10000;cw+=127)for(auto status:{0u,0x81u,0x8081u}){
        Case c;c.cpu.eip=0x43d4fb;c.cpu.r[ESP]=stack;c.limit=1;
        c.cpu.fp.put16(0,0x3f);c.cpu.fp.put16(2,std::uint16_t(status));
        c.cpu.fp.put64(8,0x12345678);c.cpu.fp.put64(16,0xabcdef00);c.cpu.fp.put16(6,0x345);
        c.words={{stack+0x20,cw}};compare(c);
        if(!(cw%381)){c.unmapped=stack;compare(c);}
    }
    for(auto cw:{0x3fu,0x7fu,0x27fu,0x37fu,0xffffu}){
        Case c;c.cpu.eip=0x43d4e3;c.cpu.r[ESP]=stack;c.limit=12;
        c.cpu.fp.put16(0,std::uint16_t(cw));c.cpu.fp.bytes[4]=1;
        c.cpu.fp.put64(32,0xc000000000000000ull);c.cpu.fp.put16(40,0x3fff);compare(c);
        c.readonly=stack;compare(c);
    }
    for(unsigned ah=0;ah<256;++ah)for(auto initial:{0u,STATUS_FLAGS}){
        Case c;c.cpu.eip=0x474a02;c.cpu.r[ESP]=stack;c.cpu.r[EAX]=0x12340078|(ah<<8);
        c.cpu.flags=initial;c.cpu.defined_flags=0;c.limit=1;compare(c);
    }
    for(auto entry:{0x47e822u,0x47548fu})for(unsigned bytes:{0u,1u,2u,15u,16u,127u,128u})for(unsigned mode=0;mode<8;++mode){
        Case c;c.cpu.eip=entry;c.cpu.r[ESP]=stack;c.cpu.r[EAX]=0x123456ab;c.cpu.r[ECX]=bytes;
        c.cpu.r[EDI]=mode==7?0xfffffffcu:0x20000ffcu;c.cpu.flags=mode==6?DF:STATUS_FLAGS;c.limit=1;
        c.budget=mode==1?0:mode==2?1:mode==3?bytes/2:1000;
        if(mode==4)c.readonly=0x20001000;if(mode==5)c.unmapped=0x20001000;
        if(mode==7)c.extra_regions={{0xfffff000,4096}};
        compare(c);
    }
    // REP consumes its inner budget atomically in the original instruction.
    // A small scheduling slice must not force an otherwise valid 32 KiB fill
    // through thousands of checked C++ stores. A truly insufficient global
    // budget still takes the oracle's exact partially completed path.
    for(auto entry:{0x47e822u,0x47548fu}){
        Case c;c.cpu.eip=entry;c.cpu.r[ESP]=stack;c.cpu.r[EAX]=0x123456ab;c.cpu.r[ECX]=8192;
        c.cpu.r[EDI]=0x20001000;c.limit=1;c.budget=20000;compare(c);
        const auto result=execute(image,c,true);CHECK(result.batches==1 && result.count==1 && result.cpu.r[ECX]==0);
        c.budget=4096;compare(c);c.budget=20000;c.unmapped=0x20003000;compare(c);
    }
    for(auto entry:{0x47ea67u,0x4757bau})for(unsigned mode=0;mode<12;++mode){
        Case c;c.cpu.eip=entry;c.cpu.r[ESP]=stack;c.cpu.r[ESI]=0x20000000;c.cpu.r[EDI]=0x20004000;
        c.cpu.r[ECX]=mode<2?0:mode==2?1:mode==3?127:mode==4?8192:1024;c.limit=1;c.budget=20000;
        if(mode==5)c.cpu.r[EDI]=c.cpu.r[ESI]+1;
        if(mode==6)c.cpu.r[EDI]=c.cpu.r[ESI]-1;
        if(mode==7)c.readonly=c.cpu.r[EDI];
        if(mode==8)c.unmapped=c.cpu.r[ESI]+4096;
        if(mode==9){c.cpu.r[ESI]=0xfffffffdu;c.extra_regions={{0xfffff000,4096}};}
        if(mode==10)c.cpu.flags=DF;if(mode==11)c.budget=512;
        compare(c);
    }
    for(unsigned offset=0;offset<32;++offset)for(unsigned mode=0;mode<3;++mode){
        Case c;c.cpu.eip=0x47e798;c.cpu.r[ESP]=stack;c.cpu.r[EDI]=0x20000fe0+offset;c.cpu.r[ECX]=2;c.limit=18;
        if(mode==1)c.readonly=0x20001000;if(mode==2)c.unmapped=0x20001000;compare(c);
    }
    {Case c;c.cpu.eip=0x43cfff;c.cpu.r[ESP]=stack;c.cpu.r[EAX]=0x12345678;c.limit=1;compare(c);
        c.corrupt=0x43d000;compare(c);}
    for(unsigned mode=0;mode<3;++mode){
        Case c;c.cpu.eip=0x47e78a;c.cpu.r[ESP]=stack;c.cpu.r[EDI]=0x20000000;c.cpu.r[ECX]=1;c.limit=18;
        if(mode==0)c.cpu.fp.put64(8,0xa123456789abcdefull);
        if(mode==1)c.cpu.fp.put64(16,0x9876543210abcdefull);
        if(mode==2)c.cpu.fp.put16(6,0xffff);
        compare(c);Fixture fixture(image,c,true);auto cpu=c.cpu;auto budget=c.budget;
        CHECK(!wa2_native_batch(cpu,fixture.memory,budget,c.limit,c.return_pc));
    }
    for(auto exponent:{0u,0x3ff0u,0x3fffu,0x4000u,0x401du,0x403fu,0x7fffu})
        for(auto sign:{0u,0x8000u})for(unsigned rounding=0;rounding<4;++rounding){
            Case c;c.cpu.eip=0x47cbd0;c.cpu.r[ESP]=stack;c.cpu.r[EBP]=stack+0x100;c.words={{stack,stop_pc}};
            c.extra_regions={{0xbc6000,0x3000}};c.limit=10000;c.budget=50000;
            c.cpu.fp.bytes[4]=1;c.cpu.fp.put64(32,0xc90fdaa22168c235ull);c.cpu.fp.put16(40,std::uint16_t(exponent|sign));
            c.cpu.fp.put32(24,0x1fbf|(rounding<<13));compare(c);
        }
    for(unsigned kind=1;kind<=3;++kind)for(unsigned variant=0;variant<40;++variant){
        Case c;c.state_api=true;c.api_kind=kind;c.api_rows=kind==1?1:kind==2?8:16;c.api_columns=kind==1?256:kind==2?33:14;
        c.cpu.eip=0x43b050;c.cpu.r[ESP]=stack;c.limit=64;c.budget=1000;c.cpu.flags=random();c.cpu.defined_flags=random();
        U32 first=variant%9,second=variant%35,value=random();
        if(kind==1){first=std::array<U32,8>{0,27,28,52,137,255,256,0xffffffff}[variant%8];second=variant&1?1:0;}
        c.words={{stack,0xf0000000},{stack+4,stop_pc},{stack+8,0x20000100},{stack+12,first},{stack+16,second},{stack+20,value}};
        if(variant==0)c.api_receiver=0x20000200;if(variant==1)c.revoke_api=true;
        if(variant==2)c.budget=1;if(variant==3)c.limit=1;if(variant==4)c.unmapped=stack;
        compare(c);
    }
    for(unsigned kind=4;kind<=6;++kind)for(unsigned variant=0;variant<40;++variant){
        Case c;c.state_api=true;c.api_kind=kind;c.api_rows=1;c.api_columns=kind==4?1:kind==5?2:16;
        c.api_address=variant&1?0xf000fff0:0xf0000030;c.custom_viewport=variant%5==2;
        c.cpu.eip=0x43b050;c.cpu.r[ESP]=stack;c.limit=64;c.budget=1000;c.cpu.flags=random();c.cpu.defined_flags=random();
        if(variant%7==0){c.extra_regions={{0xfffff000,4096}};c.cpu.r[ESP]=kind==4?0xfffffff0:0xffffffec;}
        const auto sp=c.cpu.r[ESP];const U32 first=kind==4?(variant%3==0?1u:0u):kind==5?(variant%11==0?1u:0u):variant%18;
        const U32 second=kind==5?(variant%9==0?0:variant%9==1?0x20000220:0x20000120):(variant%6==0?0x20000220:0);
        c.words={{sp,c.api_address},{sp+4,stop_pc},{sp+8,0x20000100},{sp+12,first}};
        if(kind!=4)c.words.push_back({sp+16,second});
        if(variant==0)c.api_receiver=0x20000200;if(variant==1)c.revoke_api=true;
        if(variant==2)c.budget=1;if(variant==3)c.limit=1;if(variant==4)c.unmapped=stack;
        compare(c);
    }
    for(unsigned kind=1;kind<=3;++kind){
        Case c;c.state_api=true;c.track_differences=true;c.api_kind=kind;c.api_rows=kind==1?1:kind==2?8:16;c.api_columns=kind==1?256:kind==2?33:14;
        c.api_address=0xf000fff0;c.cpu.eip=0x43b050;c.cpu.r[ESP]=stack;c.limit=64;
        const U32 first=kind==1?27:1,second=kind==1?1:2,cleanup=kind==1?16:20,stride=cleanup+4;
        c.words={{stack,c.api_address},{stack+4,0x43b050},{stack+8,0x20000100},{stack+12,first},{stack+16,second},
            {stack+stride,c.api_address},{stack+stride+4,stop_pc},{stack+stride+8,0x20000100},{stack+stride+12,first},{stack+stride+16,kind==1?0:second}};
        if(kind!=1){c.words.push_back({stack+20,0x12345678});c.words.push_back({stack+stride+20,0});}
        compare(c);
    }
    for(unsigned variant=0;variant<80;++variant){
        Case c;c.state_api=true;c.api_kind=7;c.api_rows=65536;c.api_columns=16;c.api_address=variant&1?0xf000fff0:0xf0000030;
        c.cpu.eip=0x43b050;c.cpu.r[ESP]=stack;c.limit=64;c.cpu.flags=random();c.cpu.defined_flags=random();
        const U32 value=std::array<U32,8>{0,0x20010000,0x20020000,0x20010001,0x20030000,0xffffffffu,0xffff0000,0x10000}[variant%8];
        const U32 unit=variant%19;
        c.words={{stack,c.api_address},{stack+4,stop_pc},{stack+8,0x20000100},{stack+12,unit},{stack+16,value}};
        if(variant==9)c.api_receiver=0x20000200;if(variant==10)c.revoke_api=true;if(variant==11)c.budget=1;
        if(variant==12)c.unmapped=stack;if(variant==13)c.api_rows=0x1000;
        compare(c);
        if(variant<3){const auto before=wa2_native_inline_state_api_calls();execute(image,c,true);CHECK(wa2_native_inline_state_api_calls()==before+1);}
    }
    {
        Case c;c.state_api=true;c.cpu.eip=0x474f00;c.cpu.r[ESP]=stack;c.limit=128;c.budget=1000;
        c.extra_regions={{0xbc8000,4096}};
        c.words={{stack,0xf0000000},{stack+4,stop_pc},{stack+8,0x20000100},{stack+12,1},{stack+16,2},{stack+20,0x12345678},{0xbc8310,1}};
        c.cpu.fp.bytes[4]=1;c.cpu.fp.put64(32,0xc000000000000000ull);c.cpu.fp.put16(40,0x3fff);
        const auto before=wa2_native_inline_state_api_calls();compare(c);CHECK(wa2_native_inline_state_api_calls()>before);
    }
    {Case c;c.cpu.eip=0x43b3b2;c.cpu.r[ESP]=stack;c.cpu.r[EAX]=0xcccccccc;c.cpu.r[ESI]=U32(0x43b3b8-0xab401b);c.writable_code=true;compare(c);}
    // Native stores spanning an OS page boundary must leave the same bytes on
    // success, read-only/uncommitted faults, and 32-bit address wrap faults.
    for(unsigned mode=0;mode<4;++mode){
        Case c;c.cpu.eip=0x43b3b2;c.cpu.r[ESP]=stack;c.cpu.r[EAX]=0x76543210;
        const U32 destination=mode==3?0xffffffffu:0x20000fffu;c.cpu.r[ESI]=destination-0xab401b;
        if(mode==1)c.readonly=0x20001000;
        if(mode==2)c.unmapped=0x20001000;
        if(mode==3)c.extra_regions={{0xfffff000,4096}};
        compare(c);
    }
    {auto c=transform_case();c.cpu.r[ESP]=0x07000fe3;c.readonly=0x07001000;compare(c);}
    {
        Case c;c.cpu.eip=0x43b050;c.cpu.r[ESP]=stack;c.limit=3;
        c.words={{stack,0x43b050},{stack+4,0x43b050},{stack+8,stop_pc}};compare(c);
        const auto result=execute(image,c,true);CHECK(result.batches==1 && result.count==3 && result.cpu.eip==stop_pc);
    }
    {
        // Real indirect-call block -> original one-instruction RET -> caller,
        // all within one native invocation. Also check precise stack faults.
        Case c;c.cpu.eip=0x43b7dc;c.cpu.r[ESP]=stack;c.limit=9;
        c.extra_regions={{0xb34000,4096}};
        c.words={{11748408,0x20000100},{0x20000100,0x20000200},{0x20000298,0x43b050}};compare(c);
        const auto result=execute(image,c,true);CHECK(result.batches==1 && result.count==9 && result.cpu.eip==0x43b7fd);
        // A call and return across distinct original code pages also remain
        // within one native invocation; callback return pages stay boundaries.
        U32 cross_ret{};for(auto pc:wa2_native_entries())if((pc>>12)!=0x43b && has_oracle(pc) && image.code(pc)[0]==0xc3){cross_ret=pc;break;}CHECK(cross_ret);
        c.words.back().second=cross_ret;compare(c);
        const auto crossed=execute(image,c,true);CHECK(crossed.batches==1 && crossed.count==9 && crossed.cpu.eip==0x43b7fd);
        c.return_pc=cross_ret;compare(c);c.return_pc=stop_pc;
        {
            Fixture fixture(image,c,true);auto scope=fixture.memory.execution_scope();auto cpu=c.cpu;auto budget=c.budget;
            CHECK(wa2_native_batch(cpu,fixture.memory,budget,9,c.return_pc)==9);
            fixture.memory.initialize(cross_ret,std::array<std::uint8_t,1>{0xcc});cpu=c.cpu;budget=c.budget;
            CHECK(wa2_native_batch(cpu,fixture.memory,budget,9,c.return_pc)==8 && cpu.eip==cross_ret);
            CHECK(!wa2_native_batch(cpu,fixture.memory,budget,1,c.return_pc));
            fixture.memory.initialize(cross_ret,std::array<std::uint8_t,1>{0xc3});
            CHECK(wa2_native_batch(cpu,fixture.memory,budget,1,c.return_pc)==1 && cpu.eip==0x43b7fd);
            ++cases;
        }
        c.cpu.r[ESP]=0x0700000c;compare(c);
        c.cpu.r[ESP]=stack+1;compare(c);
    }
    CHECK(batches>0 && wa2_linear_fault_count()>0);
    const auto color=color_case(),transform=transform_case();
    const auto ca=benchmark(image,color,false,100000),cb=benchmark(image,color,true,100000);equal(ca.final,cb.final,color.cpu.eip);
    const auto fa=benchmark(image,transform,false,100000),fb=benchmark(image,transform,true,100000);equal(fa.final,fb.final,transform.cpu.eip);
    Case vertex;vertex.cpu.eip=0x43bde0;vertex.cpu.r[ESP]=stack;vertex.cpu.r[EDX]=0x20;vertex.cpu.r[EDI]=0;
    vertex.words={{stack+0x10,0},{stack+0x14,0},{stack+0x48,0x20002000},{stack+0x2c,0x10}};
    {Fixture fixture(image,vertex,true);auto cpu=vertex.cpu;std::uint64_t budget=10000;
        vertex.limit=wa2_vertex_batch(cpu,fixture.memory,budget,1000,stop_pc);CHECK(vertex.limit && cpu.eip==0x43c100);}
    const auto va=benchmark(image,vertex,1,100000),vb=benchmark(image,vertex,2,100000);equal(va.final,vb.final,vertex.cpu.eip);
    std::ofstream report(argv[2]);report<<"{\"schema\":\"wa2.native-backend-tests.v1\",\"cases\":"<<cases<<",\"entries\":"<<entries_tested<<",\"oracle_pages\":"<<std::size(code_pages)
        <<",\"exact_cpu_fpu_flags_memory_budget_faults\":true,\"native_batches\":"<<batches
        <<",\"linear_fault_recoveries\":"<<wa2_linear_fault_count()<<",\"native_execution\":"<<wa2_native_report()
        <<",\"color\":{\"before_seconds\":"<<ca.seconds<<",\"after_seconds\":"<<cb.seconds<<",\"speedup\":"<<ca.seconds/cb.seconds<<"}"
        <<",\"transform\":{\"before_seconds\":"<<fa.seconds<<",\"after_seconds\":"<<fb.seconds<<",\"speedup\":"<<fa.seconds/fb.seconds<<"}"
        <<",\"vertex_color_algorithm\":{\"native_seconds\":"<<va.seconds<<",\"algorithm_seconds\":"<<vb.seconds<<",\"ratio\":"<<va.seconds/vb.seconds<<"}}\n";CHECK(report);
    std::cout<<"PASS "<<cases<<" native-backend cases; color "<<ca.seconds/cb.seconds<<"x; mixed x87 transform "<<fa.seconds/fb.seconds<<"x; vertex native/algorithm "<<va.seconds<<" / "<<vb.seconds<<" seconds\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
