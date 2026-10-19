// Bounded numerical leaf calls only. The game's startup routine is never run.
#include "process.hpp"
#include "x87_kernel.hpp"
#include "native_math.hpp"
#include "scalar_math.hpp"
#include <winrecomp/win32_state.hpp>
#include <bit>
#include <cmath>
#include <fstream>
#include <iostream>
#include <numbers>
#include <cerrno>
#include <cfenv>
#include <chrono>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
namespace a=wa2::aot;
constexpr std::uint32_t Return=0xf0001234;
extern "C" void wr_x87_217_r243(void*,void*,std::uint32_t*);
struct Leaf {
    a::Runtime runtime;
    a::Program program{runtime.process().memory};
    wr::Cpu base;
    bool prefix_stop{};
    Leaf(){runtime.load();base=runtime.process().cpu;}
    void reset(std::uint32_t entry){
        auto& p=runtime.process();p.cpu=base;p.cpu.eip=entry;prefix_stop=false;
        p.memory.store(p.cpu.r[wr::ESP],Return,32);
    }
    void run(bool atan2_reference=false){
        auto& p=runtime.process();std::uint64_t budget=32768;
        while(p.cpu.eip!=Return){
            try{if(!program.invoke(p.cpu,budget)){
                    // Only capture the numeric result of the known prefix.
                    // No instruction at the absent epilogue is synthesized.
                    if(atan2_reference && p.cpu.eip==0x485858){prefix_stop=true;return;}
                    if(!atan2_reference || !p.dispatch_api())throw wr::GuestFault(wr::FaultKind::untranslated,p.cpu.eip,"unregistered numerical leaf dependency");
                }}catch(const wr::GuestFault& error){
                if(!atan2_reference || error.kind!=wr::FaultKind::unsupported || error.address!=0x4857eb)throw;
                // Test-only, cached hardware oracle for exactly this operation.
                // It is never linked into either production host or ARM64.
                wr::x87_execute(p.cpu,wr_x87_217_r243,nullptr,499,false,false);p.cpu.eip=0x4857ed;
            }
        }
    }
};
int main(int argc,char** argv){try{
    CHECK(argc==2);Leaf leaf;auto& p=leaf.runtime.process();unsigned cases{},other_math{},powers{},angles{},angle_prefixes{},native_angles{},axis_cases{},declines{};
    std::uint64_t maximum_double_ulps{},maximum_trig_ulps{};unsigned native_trig_candidates{};
    leaf.reset(0x47cb9c);leaf.run();CHECK(p.memory.load(0xbc830c,32)==1);
    const double inputs[]={-1000.,-100.,-10.,-3.141592653589793,-1.5707963267948966,-1.,-.5,-1e-8,-0.,0.,1e-8,.5,1.,1.5707963267948966,3.141592653589793,10.,100.,1000.};
    for(auto control:{0x7fu,0x27fu,0x37fu})for(auto entry:{0x474990u,0x474d50u})for(double input:inputs){
        leaf.reset(entry);p.cpu.fp.put16(0,std::uint16_t(control));std::uint32_t flags{};auto bits=std::bit_cast<std::uint64_t>(input);
        a::fp::x87<0xdd,0,true>(&p.cpu.fp,&bits,&flags);leaf.run();
        a::fp::Environment env;const a::fp::Extended result{p.cpu.fp.get64(32),p.cpu.fp.get16(40)};
        const auto actual=std::bit_cast<double>(a::fp::to_binary(result,64,env));
        const auto expected=entry==0x474990?std::cos(input):std::sin(input);
        CHECK(std::isfinite(actual) && std::abs(actual-expected)<=2e-15);
        if(expected==0)CHECK(std::signbit(actual)==std::signbit(expected));
        CHECK(p.cpu.r[wr::ESP]==leaf.base.r[wr::ESP]+4);++cases;
        const auto candidate=entry==0x474990?wa2::math::cosine_finite(input):wa2::math::sine_finite(input);
        if(wa2::math::exact_trigonometry_input(input)){
            const auto ordered=[](std::uint64_t bits){return bits>>63?~bits:bits|(1ull<<63);};
            const auto got=ordered(std::bit_cast<std::uint64_t>(candidate.value)),wanted=ordered(std::bit_cast<std::uint64_t>(actual));
            const auto ulps=got>wanted?got-wanted:wanted-got;maximum_trig_ulps=std::max(maximum_trig_ulps,std::uint64_t(ulps));
            if(ulps)throw std::runtime_error("native trig candidate is not bit exact at "+wr::hex(entry)+" input "+std::to_string(input));
            a::fp::Environment source32,converted32;CHECK(a::fp::to_binary(result,32,source32)==a::fp::to_binary(a::fp::from_binary(std::bit_cast<std::uint64_t>(candidate.value),64,converted32),32,converted32));++native_trig_candidates;
        }
    }
    const auto result=[&]{a::fp::Environment env;return std::bit_cast<double>(a::fp::to_binary({p.cpu.fp.get64(32),p.cpu.fp.get16(40)},64,env));};
    const auto push=[&](double value){auto bits=std::bit_cast<std::uint64_t>(value);std::uint32_t flags{};a::fp::x87<0xdd,0,true>(&p.cpu.fp,&bits,&flags);};
    unsigned native_trig_returns{},trig_static_fallbacks{},weather_values{},trig_declines{},weather_divided_values{};
    const auto ordered=[](std::uint64_t bits){return bits>>63?~bits:bits|(1ull<<63);};
    const auto check_trig=[&](std::uint32_t entry,unsigned control,double input,bool divided=false){
        const auto prepare=[&]{push(input);if(divided){auto divisor=std::bit_cast<std::uint64_t>(3.141592);std::uint32_t flags{};a::fp::x87<0xdc,6,true>(&p.cpu.fp,&divisor,&flags);}};
        leaf.reset(entry);p.cpu.fp.put16(0,std::uint16_t(control));prepare();
        a::fp::Environment converted;const auto source_argument=std::bit_cast<double>(a::fp::to_binary({p.cpu.fp.get64(32),p.cpu.fp.get16(40)},64,converted));leaf.run();
        const auto reference=result();a::fp::Environment narrowing;
        const auto reference32=a::fp::to_binary({p.cpu.fp.get64(32),p.cpu.fp.get16(40)},32,narrowing);
        const auto candidate=entry==0x474990?wa2::math::cosine_finite(source_argument):wa2::math::sine_finite(source_argument);
        const bool admitted=wa2::math::exact_trigonometry_input(source_argument);
        leaf.reset(entry);p.cpu.fp.put16(0,std::uint16_t(control));push(3.25);prepare();
        for(auto reg:{wr::EBX,wr::EBP,wr::ESI,wr::EDI})p.cpu.r[reg]=0x56780000+unsigned(reg);
        const auto before=p.cpu;const auto counts=a::native_math_test_counts;
        CHECK(leaf.program.invoke_unmetered(p.cpu));CHECK(p.cpu.eip==Return && p.cpu.r[wr::ESP]==before.r[wr::ESP]+4);
        for(auto reg:{wr::EBX,wr::EBP,wr::ESI,wr::EDI})CHECK(p.cpu.r[reg]==before.r[reg]);
        CHECK(p.cpu.fp.get16(0)==control && (p.cpu.fp.get16(2)&0x3800)==(before.fp.get16(2)&0x3800));
        CHECK(p.cpu.fp.get64(48)==0xd000000000000000ull && p.cpu.fp.get16(56)==0x4000 && p.cpu.fp.bytes[4]==before.fp.bytes[4]);
        const auto count_before=entry==0x474990?counts.cosine:counts.sine;
        const auto count_after=entry==0x474990?a::native_math_test_counts.cosine:a::native_math_test_counts.sine;
        CHECK(count_after==count_before+unsigned(admitted));
        const auto got=ordered(std::bit_cast<std::uint64_t>(result())),wanted=ordered(std::bit_cast<std::uint64_t>(reference));
        const auto ulps=got>wanted?got-wanted:wanted-got;
        if(ulps)throw std::runtime_error("native trig ABI mismatch at "+wr::hex(entry)+" input "+std::to_string(input));
        CHECK(a::fp::to_binary({p.cpu.fp.get64(32),p.cpu.fp.get16(40)},32,narrowing)==reference32);
        if(admitted){maximum_trig_ulps=std::max(maximum_trig_ulps,std::uint64_t(ulps));++native_trig_returns;}else ++trig_static_fallbacks;
    };
    for(auto control:{0x7fu,0x27fu,0x37fu})for(auto entry:{0x474990u,0x474d50u})for(double input:inputs)check_trig(entry,control,input);
    // The existing weather's phase and angle span is below 115 radians.
    // Compare the actual shared AOT hook and its conservative fallback across it.
    for(auto control:{0x7fu,0x27fu,0x37fu})for(auto entry:{0x474990u,0x474d50u})for(unsigned step=0;step<=128;++step){
        check_trig(entry,control,115.*step/128);++weather_values;
    }
    for(auto control:{0x7fu,0x27fu,0x37fu})for(auto entry:{0x474990u,0x474d50u})for(unsigned step=0;step<=128;++step){
        check_trig(entry,control,360.*step/128,true);++weather_divided_values;
    }
    unsigned circle_values{};
    for(auto control:{0x7fu,0x27fu,0x37fu})for(auto entry:{0x474990u,0x474d50u}){
        for(unsigned sides:{4,8,12,16,24,32})for(unsigned vertex=0;vertex<=sides;++vertex){
            check_trig(entry,control,vertex*6.283184/sides);++circle_values;
        }
        for(int quadrant=-8;quadrant<=8;++quadrant){
            const auto angle=quadrant*std::numbers::pi_v<double>/2;
            for(double value:{std::nextafter(angle,-INFINITY),angle,std::nextafter(angle,INFINITY)}){
                // The separately compiled tiny-input handler stays outside
                // this ordinary-path comparison, including signed denormals.
                if(wa2::math::exact_trigonometry_input(value)){check_trig(entry,control,value);++circle_values;}
            }
        }
        for(double value:{std::ldexp(1.,-252),-std::ldexp(1.,-252),128.,-128.}){
            check_trig(entry,control,value);++circle_values;
        }
        std::uint32_t seed=0x917ec42b;
        for(unsigned sample=0;sample<64;++sample){
            seed=1664525u*seed+1013904223u;
            check_trig(entry,control,(double(seed)/4294967296.)*256.-128.);++circle_values;
        }
    }
    for(auto entry:{0x474990u,0x474d50u})for(unsigned kind=0;kind<12;++kind){
        leaf.reset(entry);push(kind==0?129.:kind==1?std::ldexp(1.,-260):.5);
        if(kind==2)p.cpu.fp.put32(24,p.cpu.fp.get32(24)|0x2000);
        if(kind==3)p.cpu.fp.put32(24,p.cpu.fp.get32(24)|0x40);
        if(kind==4)p.cpu.fp.put32(24,p.cpu.fp.get32(24)|0x8000);
        if(kind==5)p.cpu.fp.put32(24,p.cpu.fp.get32(24)&~0x80);
        if(kind==6)p.cpu.fp.put16(0,0x37e);
        if(kind==7)p.cpu.fp.bytes[4]=0;
        if(kind==8){p.cpu.fp.put64(32,0x8000000000000000ull);p.cpu.fp.put16(40,0x7fff);}
        if(kind==9){p.cpu.fp.put64(32,0xc000000000000000ull);p.cpu.fp.put16(40,0x7fff);}
        if(kind==10)p.memory.store(0xbc830c,0,32);
        if(kind==11)p.cpu.fp.put16(0,p.cpu.fp.get16(0)&~0x40);
        const auto before=p.cpu;const auto counts=a::native_math_test_counts;
        CHECK(!(entry==0x474990?a::native_cosine(p.cpu,p.memory):a::native_sine(p.cpu,p.memory)));
        CHECK(p.cpu.r==before.r && p.cpu.eip==before.eip && p.cpu.fp.bytes==before.fp.bytes && p.cpu.flags==before.flags);
        CHECK(a::native_math_test_counts.sine==counts.sine && a::native_math_test_counts.cosine==counts.cosine);++trig_declines;
        p.memory.store(0xbc830c,1,32);
    }
    unsigned modified_table_declines{};
    for(auto entry:{0x474990u,0x474d50u}){
        const auto address=entry==0x474990?0x4a84d0u:0x4ac710u;
        const auto byte=p.memory.load(address,8);
        const auto old=p.memory.protect(address&~4095u,4096,wr::Memory::Read|wr::Memory::Write);
        p.memory.store(address,byte^1u,8);
        leaf.reset(entry);push(.5);const auto before=p.cpu;
        CHECK(!(entry==0x474990?a::native_cosine(p.cpu,p.memory):a::native_sine(p.cpu,p.memory)));
        CHECK(p.cpu.r==before.r && p.cpu.eip==before.eip && p.cpu.fp.bytes==before.fp.bytes && p.cpu.flags==before.flags);
        p.memory.store(address,byte,8);p.memory.protect(address&~4095u,4096,old);++modified_table_declines;
    }
    struct Unary {std::uint32_t entry;double(*reference)(double);};
    // The cached fallback loads FLDLG2 before FYL2X: this entry is log10.
    const Unary unary[]={{0x477a80,static_cast<double(*)(double)>(std::log10)},
        {0x4784d0,static_cast<double(*)(double)>(std::acos)},{0x478620,static_cast<double(*)(double)>(std::asin)},
        {0x478770,static_cast<double(*)(double)>(std::atan)},{0x4788b0,static_cast<double(*)(double)>(std::tan)}};
    for(auto control:{0x7fu,0x27fu,0x37fu})for(const auto& operation:unary)for(double input:{.125,.5,.875}){
        leaf.reset(operation.entry);p.cpu.fp.put16(0,std::uint16_t(control));push(input);leaf.run();const auto actual=result(),expected=operation.reference(input);
        if(!std::isfinite(actual) || std::abs(actual-expected)>3e-15)throw std::runtime_error("numerical leaf mismatch at "+wr::hex(operation.entry)+" actual "+std::to_string(actual)+" expected "+std::to_string(expected));
        ++other_math;
    }
    const std::array<std::array<double,2>,6> power_inputs{{{2,3},{.5,2},{4,.5},{1,0},{-2,3},{-2,4}}};
    for(auto control:{0x7fu,0x27fu,0x37fu})for(auto input:power_inputs){
        leaf.reset(0x474ac0);p.cpu.fp.put16(0,std::uint16_t(control));push(input[0]);push(input[1]);leaf.run();
        CHECK(result()==std::pow(input[0],input[1]));++powers;
    }
    std::vector<std::array<std::uint64_t,4>> atan2_reference;
    // Axis cases require an uncached epilogue; these records describe only
    // the complete, known nonzero numeric paths, not an invented epilogue.
    for(double y:{-100.,-2.,-1.,-.5,.5,1.,2.,100.})for(double x:{-100.,-2.,-1.,-.5,.5,1.,2.,100.}){
        leaf.reset(0x47885a);push(y);push(x);leaf.run(true);const auto actual=result(),expected=std::atan2(y,x);
        if(!std::isfinite(actual) || std::abs(actual-expected)>3e-15)throw std::runtime_error("atan2 prefix y="+std::to_string(y)+" x="+std::to_string(x)+" actual="+std::to_string(actual)+" expected="+std::to_string(expected));++angles;
        angle_prefixes+=leaf.prefix_stop;
        a::fp::Environment env;const auto f32=a::fp::to_binary({p.cpu.fp.get64(32),p.cpu.fp.get16(40)},32,env);
        atan2_reference.push_back({std::bit_cast<std::uint64_t>(y),std::bit_cast<std::uint64_t>(x),std::bit_cast<std::uint64_t>(actual),f32});
        for(auto control:{0x7fu,0x27fu,0x37fu}){
            leaf.reset(0x47885a);p.cpu.fp.put16(0,std::uint16_t(control));push(3.25);push(y);push(x);
            for(auto reg:{wr::EBX,wr::EBP,wr::ESI,wr::EDI})p.cpu.r[reg]=0x12340000+unsigned(reg);
            const auto before=p.cpu;CHECK(leaf.program.invoke_unmetered(p.cpu));CHECK(p.cpu.eip==Return && p.cpu.r[wr::ESP]==before.r[wr::ESP]+4);
            for(auto reg:{wr::EBX,wr::EBP,wr::ESI,wr::EDI})CHECK(p.cpu.r[reg]==before.r[reg]);
            CHECK(p.cpu.fp.get16(0)==control && p.cpu.fp.get64(48)==0xd000000000000000ull && p.cpu.fp.get16(56)==0x4000);
            const auto native=result();const auto ordered=[](std::uint64_t bits){return bits>>63?~bits:bits|(1ull<<63);};
            const auto got=ordered(std::bit_cast<std::uint64_t>(native)),wanted=ordered(std::bit_cast<std::uint64_t>(actual));
            const auto ulps=got>wanted?got-wanted:wanted-got;CHECK(ulps<=1);maximum_double_ulps=std::max(maximum_double_ulps,std::uint64_t(ulps));
            a::fp::Environment narrowed;CHECK(a::fp::to_binary({p.cpu.fp.get64(32),p.cpu.fp.get16(40)},32,narrowed)==f32);++native_angles;
        }
    }
    const auto pi=std::numbers::pi_v<double>;
    const std::array<std::array<double,3>,10> axes{{{0,1,0},{-0.,1,-0.},{1,0,pi/2},{-1,0,-pi/2},{0,-1,pi},{-0.,-1,-pi},{0,0,0},{-0.,0,-0.},{0,-0.,0},{-0.,-0.,-0.}}};
    for(auto control:{0x7fu,0x27fu,0x37fu})for(const auto& v:axes){
        leaf.reset(0x47885a);p.cpu.fp.put16(0,std::uint16_t(control));push(v[0]);push(v[1]);CHECK(leaf.program.invoke_unmetered(p.cpu));CHECK(p.cpu.eip==Return);
        CHECK(std::bit_cast<std::uint64_t>(result())==std::bit_cast<std::uint64_t>(v[2]) && p.cpu.fp.get16(0)==control);++axis_cases;
    }
    for(unsigned kind=0;kind<6;++kind){
        leaf.reset(0x47885a);push(1);push(2);
        if(kind==0)p.cpu.fp.put16(0,0x37e);if(kind==1)p.cpu.fp.put16(0,0x77f);if(kind==2)p.cpu.fp.bytes[4]=0;
        if(kind==3)p.cpu.eip=0xdeadbeef;if(kind==4){p.cpu.fp.put64(32,0x8000000000000001ull);p.cpu.fp.put16(40,0x3fff);}
        if(kind==5){p.cpu.fp.put64(32,0xc000000000000000ull);p.cpu.fp.put16(40,0x7fff);}
        const auto before=p.cpu;CHECK(!a::native_atan2(p.cpu,p.memory));CHECK(p.cpu.r==before.r && p.cpu.eip==before.eip && p.cpu.fp.bytes==before.fp.bytes && p.cpu.flags==before.flags);++declines;
    }
    std::fenv_t saved;CHECK(std::fegetenv(&saved)==0);CHECK(std::fesetround(FE_DOWNWARD)==0);std::feclearexcept(FE_ALL_EXCEPT);std::feraiseexcept(FE_INVALID);const auto prior_errno=errno;errno=EDOM;
    const auto native_result=wa2::math::atan2_finite(.5,2.);CHECK(std::isfinite(native_result.value));
    CHECK(std::isfinite(wa2::math::sine_finite(.5).value) && std::isfinite(wa2::math::cosine_finite(.5).value));
    CHECK(std::fegetround()==FE_DOWNWARD && std::fetestexcept(FE_ALL_EXCEPT)==FE_INVALID && errno==EDOM);CHECK(std::fesetenv(&saved)==0);errno=prior_errno;
    unsigned fallback_stops{};
    for(auto entry:{0x474990u,0x474d50u}){
        p.memory.store(0xbc830c,0,32);leaf.reset(entry);double input=.5;auto bits=std::bit_cast<std::uint64_t>(input);std::uint32_t flags{};
        a::fp::x87<0xdd,0,true>(&p.cpu.fp,&bits,&flags);
        bool rejected{};try{leaf.run();}catch(const wr::GuestFault& error){rejected=error.kind==wr::FaultKind::unsupported && error.address==(entry==0x474990?0x4749fd:0x474dbd);}
        CHECK(rejected);++fallback_stops;
    }
    unsigned cache_cases{};
    for(double input:{-0.,0.,.125,.5,-115.,115.}){
        const auto sine=wa2::math::sine_finite_uncached(input),cosine=wa2::math::cosine_finite_uncached(input);
        for(int rounding:{FE_TONEAREST,FE_UPWARD,FE_DOWNWARD}){
            std::fenv_t outer;CHECK(!std::feholdexcept(&outer));CHECK(!std::fesetround(rounding));CHECK(!std::feraiseexcept(FE_INVALID));
            const auto prior=errno;errno=EDOM;
            for(unsigned repeat=0;repeat<3;++repeat){const auto s=wa2::math::sine_finite(input),c=wa2::math::cosine_finite(input);
                CHECK(std::bit_cast<std::uint64_t>(s.value)==std::bit_cast<std::uint64_t>(sine.value) && s.exceptions==sine.exceptions);
                CHECK(std::bit_cast<std::uint64_t>(c.value)==std::bit_cast<std::uint64_t>(cosine.value) && c.exceptions==cosine.exceptions);
                CHECK(std::fegetround()==rounding && std::fetestexcept(FE_ALL_EXCEPT)==FE_INVALID && errno==EDOM);++cache_cases;}
            CHECK(!std::fesetenv(&outer));errno=prior;
        }
    }
    std::array<std::array<std::uint64_t,2>,8> cache_times{};std::uint64_t cache_checksum{};
    volatile double repeated_angle=0.;
    wa2::math::sine_finite(0.);wa2::math::cosine_finite(0.);
    for(unsigned pair=0;pair<cache_times.size();++pair)for(unsigned order=0;order<2;++order){
        const auto cached=(pair+order)&1;const auto begin=std::chrono::steady_clock::now();
        for(unsigned n=0;n<256;++n){const auto input=repeated_angle;const auto sine=cached?wa2::math::sine_finite(input):wa2::math::sine_finite_uncached(input);
            const auto cosine=cached?wa2::math::cosine_finite(input):wa2::math::cosine_finite_uncached(input);
            cache_checksum=std::rotl(cache_checksum,1)^std::bit_cast<std::uint64_t>(sine.value)^std::bit_cast<std::uint64_t>(cosine.value);}
        cache_times[pair][cached]=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-begin).count();
    }
    std::array<std::array<std::uint64_t,2>,8> leaf_times{};
    p.memory.store(0xbc830c,1,32);
    for(unsigned pair=0;pair<leaf_times.size();++pair)for(unsigned order=0;order<2;++order){
        const auto native=(pair+order)&1;const auto begin=std::chrono::steady_clock::now();
        for(unsigned n=0;n<16;++n){
            leaf.reset(0x474990);push(std::numbers::pi_v<double>/2);
            a::native_math_ports_enabled=bool(native);
            CHECK(leaf.program.invoke_unmetered(p.cpu) && p.cpu.eip==Return);
            cache_checksum=std::rotl(cache_checksum,1)^std::bit_cast<std::uint64_t>(result());
        }
        a::native_math_ports_enabled=true;
        leaf_times[pair][native]=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-begin).count();
    }
    const auto cache=wa2::math::trigonometry_cache_stats();CHECK(cache.hits>=4096 && cache.misses>0);
    std::ofstream report(argv[1]);report<<"{\"schema\":\"wa2.crt-numerical-leaves.v1\",\"sse_feature_setup\":true,\"finite_sine_cosine_values\":"<<cases
        <<",\"finite_log_and_inverse_trig_values\":"<<other_math<<",\"finite_power_values\":"<<powers<<",\"two_argument_angles\":"<<angles
        <<",\"native_atan2_returns\":"<<native_angles<<",\"native_axis_and_signed_zero_cases\":"<<axis_cases<<",\"maximum_binary64_ulps_from_cached_nonzero_numeric_prefix\":"<<maximum_double_ulps
        <<",\"native_trig_candidate_cases\":"<<native_trig_candidates<<",\"maximum_trig_candidate_ulps\":"<<maximum_trig_ulps
        <<",\"native_sine_cosine_abi_returns\":"<<native_trig_returns<<",\"sine_cosine_static_fallbacks\":"<<trig_static_fallbacks
        <<",\"weather_phase_values\":"<<weather_values<<",\"native_unary_contract_declines_unchanged\":"<<trig_declines
        <<",\"weather_divided_phase_values\":"<<weather_divided_values
        <<",\"circle_phase_bit_exact_cases\":"<<circle_values<<",\"modified_table_declines_unchanged\":"<<modified_table_declines
        <<",\"native_trig_cache_exact_environment_cases\":"<<cache_cases<<",\"trig_cache_hits\":"<<cache.hits<<",\"trig_cache_misses\":"<<cache.misses
        <<",\"repeated_angle_pairs_per_sample\":256,\"repeated_angle_cache_checksum\":"<<cache_checksum<<",\"repeated_angle_cache_ns\":[";
    for(unsigned n=0;n<cache_times.size();++n){if(n)report<<',';report<<'['<<cache_times[n][0]<<','<<cache_times[n][1]<<']';}
    report<<']'
        <<",\"near_axis_leaf_calls_per_sample\":16,\"near_axis_static_native_ns\":[";
    for(unsigned n=0;n<leaf_times.size();++n){if(n)report<<',';report<<'['<<leaf_times[n][0]<<','<<leaf_times[n][1]<<']';}
    report<<']'
        <<",\"native_math_ports_called\":{\"atan2\":"<<a::native_math_test_counts.atan2<<",\"sine\":"<<a::native_math_test_counts.sine<<",\"cosine\":"<<a::native_math_test_counts.cosine<<'}'
        <<",\"native_binary32_matches_cached_prefix\":true,\"native_contract_declines_unchanged\":"<<declines<<",\"host_math_environment_preserved\":true"
        <<",\"atan2_reference_uses_test_only_cached_x87_oracle\":true,\"atan2_known_numeric_prefix_stops\":"<<angle_prefixes
        <<",\"atan2_original_epilogue_verified\":false,\"atan2_reference\":[";
    for(unsigned n=0;n<atan2_reference.size();++n){if(n)report<<',';const auto& v=atan2_reference[n];report<<'['<<v[0]<<','<<v[1]<<','<<v[2]<<','<<v[3]<<']';}
    report<<']'
        <<",\"explicit_x87_fallback_stops\":"<<fallback_stops<<",\"maximum_instruction_budget_per_leaf\":32768,"
        "\"game_startup_executed\":false,\"audio_device_opened\":false,\"screen_access\":false,\"game_launched\":false,\"passed\":true}\n";
    std::cout<<"PASS bounded CRT feature setup and "<<cases<<" sine/cosine values; explicit x87 boundaries retained.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
