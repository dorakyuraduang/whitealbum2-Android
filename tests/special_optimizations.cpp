// Finite constructed RAM only. No game, resources, window, screen or device.
#include "../recompiled/special/runtime.hpp"
#include "../recompiled/special/optimizations.hpp"
#include "optimization_profile.hpp"
#include "profile_write_callers.hpp"
#include "bodies.hpp"
#include "../recompiled/extended80.hpp"
#include "../recompiled/text_jump_budget.hpp"
#include "../recompiled/text_page_replay_control.hpp"
#include <winrecomp/win32_state.hpp>
#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("optimization check at ")+std::to_string(__LINE__)+": " #x);}while(false)
namespace s=wa2::special;namespace a=wa2::aot;using wr::U32;
constexpr U32 Returned=0xf0001234;
bool oracle(wr::Cpu& s,wr::Memory& m){std::uint64_t budget=1000000;switch(s.eip){
#include "optimization_oracle.inc"
default:return false;}}
bool same(const wr::Cpu& x,const wr::Cpu& y){return x.r==y.r && x.eip==y.eip && x.flags==y.flags && x.defined_flags==y.defined_flags && x.fp.bytes==y.fp.bytes;}
void original(wr::Cpu& cpu,wr::Memory& m,U32 end=Returned){for(unsigned n=0;cpu.eip!=end;++n){CHECK(n<65536);if(!oracle(cpu,m))throw std::runtime_error("Missing finite oracle at "+wr::hex(cpu.eip));}}
std::vector<a::Function> references;
a::Location reference_locate(U32 pc)noexcept{auto l=s::locate(pc);if(!l.function)return {};return {&references[l.function-s::functions().data()],l.resume};}
void serialized_text(wr::Cpu& c,wr::Memory& m,a::Scope& v,std::uint64_t& b,bool,U32 r){s::f_447f60<false,false>(c,m,v,b,r);}
void serialized_scene(wr::Cpu& c,wr::Memory& m,a::Scope& v,std::uint64_t& b,bool,U32 r){s::f_44aeb0<false,false>(c,m,v,b,r);}
struct Counts {unsigned ftol{},math{},layout{},text{},scene{},declines{},guards{},cache_guards{};std::uint64_t layout_hits{},page_hits{},jumps{};};
void fixture(bool linear,Counts& counts){
    wr::ProcessOptions options;options.prefer_linear_memory=linear;s::Runtime runtime(options);auto& p=runtime.process();auto& m=p.memory;
    std::function<U32(wr::Process&)> profile_write;
    p.replace_api("kernel32.dll","WritePrivateProfileStringA",4,[&](wr::Process& q){CHECK(profile_write);return profile_write(q);});
    p.load(s::initial_image());const U32 data=p.allocate_bytes(65536),bp=data+0x8000;
    auto base=p.cpu;base.r[wr::ESP]=bp;base.fp.put16(0,0x7f);base.fp.put32(24,0x1f80);
    m.store(s::integer_ftol_source.feature,1,32);m.store(s::sine_source.feature,1,32);
    a::StaticProgram program(m,s::functions(),s::locate),reference(m,references,reference_locate);
    std::array<std::uint8_t,2048> bytes;for(unsigned n=0;n<bytes.size();++n)bytes[n]=std::uint8_t(n*37+19);
    // Exercise the actual static hook, not just the shared helper in isolation.
    for(std::int64_t value:{0ll,1ll,-1ll,28ll,-255ll,2147483647ll,-2147483648ll})for(unsigned alignment=0;alignment<8;++alignment)for(unsigned top=0;top<8;++top)for(unsigned cw:{0x7fu,0x27fu,0xf7fu})for(unsigned mxcsr:{0x1f80u,0xffa5u}){
        auto c=base;for(unsigned n=0;n<8;++n)c.r[n]=0x12340000+n*0x101;c.r[wr::ESP]=bp+128+alignment;c.eip=s::integer_ftol_source.entry;c.flags=0xa97;c.defined_flags=0x89;
        c.fp.put16(0,std::uint16_t(cw));c.fp.put16(2,std::uint16_t((top<<11)|0x4725));c.fp.put32(24,mxcsr);c.fp.bytes[4]=std::uint8_t(0xa5u|(1u<<top));
        for(unsigned n=0;n<8;++n){c.fp.put64(32+n*16,0x8000000000000000ull+n*0x100);c.fp.put16(40+n*16,0x4000);}
        const auto v=a::fp::from_integer(value);c.fp.put64(32,v.significand);c.fp.put16(40,v.sign_exponent);
        const auto reset=[&]{m.copy_in(bp-1024,bytes);m.store(c.r[wr::ESP],Returned,32);};
        reset();auto expected=c;original(expected,m);std::array<std::uint8_t,2048> wanted;m.copy_out(bp-1024,wanted);
        reset();auto actual=c;const auto activity=s::scalar_optimization_activity();CHECK(program.invoke(actual));std::array<std::uint8_t,2048> got;m.copy_out(bp-1024,got);
        if(!same(expected,actual) || wanted!=got || s::scalar_optimization_activity()!=activity+1){std::cerr<<"ftol value="<<value<<" align="<<alignment<<" top="<<top<<" cw="<<cw<<" activity="<<s::scalar_optimization_activity()-activity<<" flags="<<expected.flags<<'/'<<actual.flags<<" defined="<<expected.defined_flags<<'/'<<actual.defined_flags<<'\n';for(unsigned n=0;n<512;++n)if(expected.fp.bytes[n]!=actual.fp.bytes[n])std::cerr<<"fp["<<n<<"]="<<unsigned(expected.fp.bytes[n])<<'/'<<unsigned(actual.fp.bytes[n])<<'\n';for(unsigned n=0;n<8;++n)if(expected.r[n]!=actual.r[n])std::cerr<<"r["<<n<<"]="<<expected.r[n]<<'/'<<actual.r[n]<<'\n';CHECK(false);}++counts.ftol;
    }
    for(unsigned alignment:{1u,2u,3u}){auto c=base;c.r[wr::ESP]=bp+alignment;c.eip=s::integer_ftol_source.entry;c.fp.put64(32,0x8000000000000000ull);c.fp.put16(40,0x4000);c.fp.bytes[4]=1;
        const auto reset=[&]{m.copy_in(bp-1024,bytes);m.store(c.r[wr::ESP],Returned,32);};reset();auto expected=c;original(expected,m);std::array<std::uint8_t,2048> wanted;m.copy_out(bp-1024,wanted);
        reset();auto actual=c;const auto activity=s::scalar_optimization_activity();CHECK(program.invoke(actual));std::array<std::uint8_t,2048> got;m.copy_out(bp-1024,got);CHECK(same(expected,actual) && wanted==got && s::scalar_optimization_activity()==activity);++counts.declines;
    }
    const auto push=[](wr::Cpu& c,double value){std::uint32_t flags{};auto bits=std::bit_cast<std::uint64_t>(value);a::fp::x87<0xdd,0,true>(&c.fp,&bits,&flags);};
    const auto result=[](const wr::Cpu& c){a::fp::Environment e;return a::fp::to_binary({c.fp.get64(32),c.fp.get16(40)},64,e);};
    for(U32 entry:{s::sine_source.entry,s::cosine_source.entry})for(unsigned cw:{0x7fu,0x27fu,0x37fu})for(double input:{-128.,-12.5,-3.25,-1.,-.5,-0.,0.,.5,1.,3.25,12.5,128.}){
        auto c=base;c.eip=entry;c.fp.put16(0,std::uint16_t(cw));push(c,3.25);push(c,input);m.store(bp,Returned,32);
        auto expected=c;{s::OptimizationReferenceScope source;for(unsigned n=0;expected.eip!=Returned;++n){CHECK(n<4096);CHECK(program.invoke(expected));}}
        auto actual=c;const auto activity=s::scalar_optimization_activity();CHECK(program.invoke(actual));if(actual.eip!=Returned || actual.r[wr::ESP]!=bp+4 || result(actual)!=result(expected) || s::scalar_optimization_activity()!=activity+1){std::cerr<<"trig entry="<<wr::hex(entry)<<" input="<<input<<" cw="<<cw<<" pc="<<wr::hex(actual.eip)<<" activity="<<s::scalar_optimization_activity()-activity<<" result="<<std::hex<<result(expected)<<'/'<<result(actual)<<std::dec<<'\n';CHECK(false);}
        for(unsigned r:{wr::EBX,wr::ESI,wr::EDI,wr::EBP})CHECK(actual.r[r]==c.r[r]);
        CHECK(actual.fp.get16(0)==cw && actual.fp.bytes[4]==c.fp.bytes[4] && actual.fp.get64(48)==c.fp.get64(48));++counts.math;
    }
    for(unsigned cw:{0x7fu,0x27fu,0x37fu})for(double x:{-100.,-2.,-.5,.5,2.,100.})for(double y:{-100.,-2.,-.5,.5,2.,100.}){
        auto c=base;c.eip=s::atan2_entry;c.fp.put16(0,std::uint16_t(cw));push(c,3.25);push(c,y);push(c,x);m.store(bp,Returned,32);
        auto expected=c;{s::OptimizationReferenceScope source;for(unsigned n=0;expected.eip!=Returned;++n){CHECK(n<4096);CHECK(program.invoke(expected));}}
        auto actual=c;const auto activity=s::scalar_optimization_activity();CHECK(program.invoke(actual));const auto ordered=[](std::uint64_t v){return v>>63?~v:v|(1ull<<63);};const auto actual_bits=ordered(result(actual)),expected_bits=ordered(result(expected));CHECK((actual_bits>expected_bits?actual_bits-expected_bits:expected_bits-actual_bits)<=1 && actual.eip==Returned && actual.r[wr::ESP]==bp+4 && s::scalar_optimization_activity()==activity+1);
        a::fp::Environment f,g;CHECK(a::fp::to_binary({actual.fp.get64(32),actual.fp.get16(40)},32,f)==a::fp::to_binary({expected.fp.get64(32),expected.fp.get16(40)},32,g));++counts.math;
    }
    // Layout cache against complete original instruction blocks and their CRT
    // calls, including low stack bytes, all registers and all 512 FP bytes.
    const U32 sprite=data+0x1000,string=data+0x2000,done=data+0x3000,handles=data+0x4000;
    std::array<std::uint8_t,256> blank{};m.copy_in(sprite,blank);m.store(sprite+0x50,0,16);
    m.store(s::text_profile.font_metrics,32,8);m.store(s::text_profile.font_metrics+1,2,8);m.store(s::text_profile.font_metrics+2,38,8);
    for(unsigned outline:{0u,1u})for(unsigned size:{24u,28u,32u})for(unsigned opacity:{0u,127u,255u,300u})for(unsigned cw:{0x7fu,0x27fu,0x37fu}){
        auto c=base;c.eip=s::text_profile.layout_begin;c.r[wr::EBP]=bp;c.r[wr::ESP]=bp-s::text_profile.frame_bytes;c.fp.put16(0,std::uint16_t(cw));
        m.copy_in(bp-1024,bytes);m.store(bp-0xf8,sprite,32);m.store(bp-0xd4,string,32);m.store(bp-0xe4,size,32);m.store(bp-0xcc,opacity,32);m.store(bp-0xd0,37,32);m.store(bp-0xd8,53,32);m.store(bp+0x50,outline,32);
        std::array<std::uint8_t,2048> before;m.copy_out(bp-1024,before);auto expected=c;const auto end=outline?s::text_profile.outline_end:s::text_profile.fill_end;original(expected,m,end);std::array<std::uint8_t,2048> wanted;m.copy_out(bp-1024,wanted);
        m.copy_in(bp-1024,before);auto captured=c;{a::SourceTextReplay replay(m,s::text_profile,true);auto scope=m.execution_scope();CHECK(!replay.layout_begin(captured,scope));original(captured,m,end);replay.layout_finish(captured,scope,end);}
        m.copy_in(bp-1024,before);auto actual=c;{a::SourceTextReplay replay(m,s::text_profile,true);auto scope=m.execution_scope();actual.eip=replay.layout_begin(actual,scope);CHECK(actual.eip==end);}
        std::array<std::uint8_t,2048> got;m.copy_out(bp-1024,got);if(!same(expected,actual) || wanted!=got){std::cerr<<"layout outline="<<outline<<" size="<<size<<" opacity="<<opacity<<" cw="<<cw<<'\n';CHECK(false);}++counts.layout;
    }
    // Full source text calls use synthetic font atlases and read-only recorded
    // submissions. The original and cached paths share the native glyph ABI.
    const auto draw=s::drawing_source;
    m.copy_in(sprite,blank);m.store(sprite+0x48,0xffff,16);m.store(sprite+0x4a,0,16);m.store(sprite+0x4c,0xffff,16);
    m.store(sprite+0x80,0x3f800000,32);m.store(sprite+0x84,0x3f800000,32);m.store(sprite+0x15,4,8);
    m.store(draw.font_styles,32,8);
    m.store(draw.globals+4,1,32);m.store(draw.globals+0x438,0xe0001100,32);m.store(draw.globals+0x43c,0xe0001200,32);m.store(draw.globals,0xe0001200,32);
    for(unsigned n=0;n<256;++n)m.store(handles+n*4,0xe0010000+n*16,32);
    for(unsigned image=0;image<3;++image){const auto row=draw.images+image*128;m.copy_in(row,std::span(blank).first(128));m.store(row,1,8);m.store(row+0x70,handles,32);
        for(auto [off,v]:{std::pair{0x5c,3200},{0x5e,3840},{0x60,256},{0x62,256},{0x64,13},{0x66,15},{0x68,1}})m.store(row+off,v,16);}
    std::array<std::uint8_t,128> text{};for(unsigned n=0;n<32;++n){text[n*2]=0x81;text[n*2+1]=0x40+std::uint8_t(n%8);}m.copy_in(string,text);
    std::vector<std::vector<std::uint8_t>> commands;
    struct Sink {decltype(commands)* out;wr::Cpu* cpu{};unsigned calls{},failure{},prepared{};bool decline{};
        bool record(const a::SpriteCommand& c){if(++calls==failure)throw wr::GuestFault(wr::FaultKind::unsupported,cpu->eip,"finite draw fault");
            const std::array<U32,14> state{c.device,c.target,U32(c.target_kind),c.blend_enabled,c.blend_op,c.source_blend,c.destination_blend,c.minimum,c.magnification,c.mipmap,c.alpha_argument,c.surface_result,U32(c.glyph),U32(c.particle)};
            auto& row=out->emplace_back(sizeof(state)+c.tiles.size_bytes());std::memcpy(row.data(),state.data(),sizeof(state));if(!c.tiles.empty())std::memcpy(row.data()+sizeof(state),c.tiles.data(),c.tiles.size_bytes());return true;}
    } sink{&commands};
    a::SpriteService service(m,[](const a::SpriteCommand& c,void* owner){return static_cast<Sink*>(owner)->record(c);},&sink,a::SurfaceGlyphAccess::read_only,
        [](const a::SpriteCommand& c,std::uint64_t serial,unsigned index,void* owner){auto& sink=*static_cast<Sink*>(owner);CHECK(serial>=128 && index<8);++sink.prepared;if(sink.decline)return false;return sink.record(c);});
    struct TextResult {wr::Cpu cpu;std::array<std::uint8_t,2048> stack;U32 remainder,status;decltype(commands) calls;std::optional<std::pair<wr::FaultKind,U32>> fault;};
    const auto text_call=[&](bool native,unsigned outline,unsigned cw,unsigned limit=0xffffffff,unsigned quota=256){
        m.copy_in(bp-1024,bytes);commands.clear();auto c=base;c.r[wr::ESP]=bp;c.eip=s::text_profile.entry;c.fp.put16(0,std::uint16_t(cw));m.store(bp,Returned,32);m.store(done,0,32);m.store(s::text_profile.remainder,0,32);
        sink.cpu=&c;sink.calls=sink.prepared=0;
        const std::array<U32,25> args{sprite,10,20,80,10,0,0,0,0,0,28,string,limit,0,255,255,255,255,outline,12,34,56,0,0,done};
        for(unsigned n=0;n<args.size();++n)m.store(bp+4+n*4,args[n],32);
        std::optional<s::OptimizationReferenceScope> source;if(!native)source.emplace();
        std::optional<std::pair<wr::FaultKind,U32>> fault;
        try{for(unsigned n=0;c.eip!=Returned;++n){CHECK(n<65536);a::GlyphCallScope allowance(m,quota);CHECK((native?program:reference).invoke(c));}}
        catch(const wr::GuestFault& e){fault={{e.kind,e.address}};}
        TextResult r{c,{},m.load(s::text_profile.remainder,32),m.load(done,32),commands,fault};m.copy_out(bp-1024,r.stack);return r;
    };
    const auto compare=[&](const TextResult& expected,const TextResult& got){if(!same(expected.cpu,got.cpu) || expected.stack!=got.stack || expected.remainder!=got.remainder || expected.status!=got.status || expected.calls!=got.calls || expected.fault!=got.fault){std::cerr<<"text result pc="<<wr::hex(got.cpu.eip)<<" calls="<<expected.calls.size()<<'/'<<got.calls.size()<<" cpu="<<same(expected.cpu,got.cpu)<<" stack="<<(expected.stack==got.stack)<<'\n';CHECK(false);}};
    const auto start=a::source_text_counts;const auto jumps=a::text_jump_counts.local_jumps;
    for(unsigned outline:{0u,1u})for(unsigned cw:{0x7fu,0x27fu,0x37fu})for(unsigned n=0;n<4;++n){const auto expected=text_call(false,outline,cw),got=text_call(true,outline,cw);compare(expected,got);CHECK(!expected.fault && !got.fault && !got.calls.empty() && got.cpu.eip==Returned);++counts.text;}
    for(unsigned quota:{1u,3u,7u}){compare(text_call(false,0,0x7f,0xffffffff,quota),text_call(true,0,0x7f,0xffffffff,quota));++counts.cache_guards;}
    for(auto [at,value]:std::array<std::pair<U32,U32>,4>{{{handles,0xe00f0000},{draw.globals+0x43c,0xe00f1000},{draw.palette,0xff778899},{s::text_profile.font_metrics+1,3}}}){
        const unsigned width=at==s::text_profile.font_metrics+1?8:32;const auto old=m.load(at,width);m.store(at,value,width);compare(text_call(false,0,0x7f),text_call(true,0,0x7f));m.store(at,old,width);++counts.cache_guards;}
    text_call(true,0,0x7f);sink.decline=true;const auto ordinary=text_call(false,0,0x7f),declined=text_call(true,0,0x7f);CHECK(sink.prepared);compare(ordinary,declined);sink.decline=false;++counts.cache_guards;
    text_call(true,0,0x7f);sink.failure=6;const auto fault_before=text_call(false,0,0x7f),fault_after=text_call(true,0,0x7f);CHECK(fault_before.fault && fault_after.fault && sink.prepared);compare(fault_before,fault_after);sink.failure=0;++counts.cache_guards;
    // Actual markup table JMPs, output/typewriter and wrap departures retain
    // the original path. These calls also verify bounded local dispatch.
    for(const std::string value:{std::string("ABC"),std::string("\x81\x40\n\x81\x41"),std::string("\x81\x40")}){
        text.fill(0);std::copy(value.begin(),value.end(),text.begin());m.copy_in(string,text);compare(text_call(false,0,0x7f),text_call(true,0,0x7f));++counts.text;
    }
    counts.layout_hits+=a::source_text_counts.layout_hits-start.layout_hits;counts.page_hits+=a::source_text_counts.page_spans-start.page_spans;counts.jumps+=a::text_jump_counts.local_jumps-jumps;
    CHECK(a::source_text_counts.page_spans>start.page_spans && a::source_text_counts.layout_hits>start.layout_hits && counts.jumps);
    // The second new register-resident hotspot is the renderer's merged
    // owner. Check its complete empty return and precise floating fault
    // prefixes without creating a renderer/device or executing a frame.
    for(unsigned cw:{0x7fu,0x27fu,0x37fu,0xf7fu})for(unsigned mode=0;mode<3;++mode){
        struct Result {wr::Cpu cpu;std::vector<std::uint8_t> ram;std::optional<std::pair<wr::FaultKind,U32>> fault;};
        const auto run=[&](bool native){auto c=base;c.eip=mode==0?0x44aeb0:mode==1?0x443c2d:0x443c3a;c.fp.put16(0,std::uint16_t(cw));m.store(0xb31374,0,32);m.store(bp,Returned,32);
            if(mode==1)c.r[wr::EAX]=0xde000000;if(mode==2){push(c,1);c.r[wr::ESP]=0xde000000;}
            Result out{};try{for(unsigned n=0;c.eip!=Returned;++n){CHECK(n<128);CHECK((native?program:reference).invoke(c));}}catch(const wr::GuestFault& e){out.fault={{e.kind,e.address}};}
            out.cpu=c;out.ram.resize(0x3000);m.copy_out(bp-0x2800,out.ram);return out;};
        const auto expected=run(false),got=run(true);CHECK(same(expected.cpu,got.cpu) && expected.ram==got.ram && expected.fault==got.fault && bool(got.fault)==(mode!=0));++counts.scene;
    }
    // SC exit-save scope is admitted only through its original wrapper and
    // static caller range, with matching section/key and no open writers.
    p.cpu=base;const auto& w=s::profile_write_source;const auto sp=p.cpu.r[wr::ESP];m.store(w.update,17,32);m.store(sp,w.api_return,32);m.store(sp+w.caller_slot,w.caller_first,32);m.store(sp+4,string,32);m.store(sp+w.section_slot,string,32);m.store(sp+8,sprite,32);m.store(sp+w.key_slot,sprite,32);
    CHECK(a::source_profile_write_scope(p,w,s::locate)==17);++counts.guards;
    for(unsigned n=0;n<4;++n){if(n==0)m.store(sp,w.api_return+1,32);if(n==1){m.store(sp,w.api_return,32);m.store(sp+w.caller_slot,w.caller_first-1,32);}if(n==2){m.store(sp+w.caller_slot,w.caller_first,32);m.store(sp+w.key_slot,sprite+1,32);}if(n==3){m.store(sp+w.key_slot,sprite,32);p.state().current_thread_id=2;}CHECK(!a::source_profile_write_scope(p,w,s::locate));++counts.guards;}
    p.state().current_thread_id=1;unsigned writes{};
    profile_write=[&](wr::Process& q){CHECK(a::source_profile_write_scope(q,w,s::locate)==17);++writes;return 1u;};
    const auto section=p.put_string("DEFAULT"),key=p.put_string("volume"),file=p.put_string("fixture.ini");
    for(auto caller:s::profile_write_callers){p.cpu=base;p.cpu.eip=0x44f1a0;m.store(sp,caller,32);const std::array<U32,4> args{key,75,section,file};for(unsigned n=0;n<4;++n)m.store(sp+4+n*4,args[n],32);
        for(unsigned n=0;p.cpu.eip!=caller;++n){CHECK(n<128);if(!program.invoke(p.cpu))CHECK(p.dispatch_api());}CHECK(p.cpu.r[wr::ESP]==sp+4);++counts.guards;}
    CHECK(writes==53);
    CHECK(p.executed_instructions==0 && !p.gui() && !p.directdraw());
}
int main(int argc,char** argv){try{CHECK(argc==2);references.assign(s::functions().begin(),s::functions().end());references[s::locate(0x447f60).function-s::functions().data()].body=serialized_text;references[s::locate(0x44aeb0).function-s::functions().data()].body=serialized_scene;
    Counts c;for(bool linear:{false,true})fixture(linear,c);
    std::ofstream report(argv[1]);report<<"{\"passed\":true,\"integer_source_cases\":"<<c.ftol<<",\"trig_source_cases\":"<<c.math<<",\"exact_layout_cases\":"<<c.layout<<",\"actual_text_calls\":"<<c.text<<",\"hot_scene_prefix_cases\":"<<c.scene<<",\"actual_layout_hits\":"<<c.layout_hits<<",\"actual_page_spans\":"<<c.page_hits<<",\"local_jumps\":"<<c.jumps<<",\"write_scope_guards\":"<<c.guards<<",\"cache_guard_cases\":"<<c.cache_guards<<",\"cross_page_source_fallbacks\":"<<c.declines<<",\"game_launched\":false,\"screen_access\":false,\"source_executable_opened\":false,\"device_performance_verified\":false}\n";CHECK(report);std::cout<<"PASS SC scalar/text source equivalence and actual memo admission.\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
