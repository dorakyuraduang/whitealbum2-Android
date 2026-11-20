// Bounded source drawing calls against constructed RAM and recorded CPU APIs.
// No startup, resource files, window, GL context, audio device or frame loop.
#include "../recompiled/special/runtime.hpp"
#include "bodies.hpp"
#include "../platform/render/submission_cache.hpp"
#include <winrecomp/win32_state.hpp>
#include <bit>
#include <fstream>
#include <iostream>

enum class Kind {unknown,d3d,device,texture,surface,vertexbuffer,indexbuffer,vertexshader,pixelshader,declaration,stateblock,swapchain,buffer,base_texture,resource};
#include "../port/d3d9_tables.inc"
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed at ")+std::to_string(__LINE__)+": " #x);}while(false)
namespace s=wa2::special;
namespace a=wa2::aot;
using wr::U32;

std::vector<a::Function> reference_functions;
a::Location reference_locate(U32 pc) noexcept {
    const auto location=s::locate(pc);
    if(!location.function)return {};
    return {&reference_functions[location.function-s::functions().data()],location.resume};
}
void serialized(wr::Cpu& cpu,wr::Memory& memory,a::Scope& scope,std::uint64_t& budget,bool,U32 resume){
    s::f_43cb90<false,false>(cpu,memory,scope,budget,resume);
}
struct Call {
    std::string name;std::vector<U32> arguments;std::vector<std::uint8_t> vertices;
    bool operator==(const Call&)const=default;
};
struct Result {
    wr::Cpu cpu;std::vector<Call> calls;
    std::vector<std::uint8_t> data,stack,globals;
    bool fault{};wr::FaultKind kind{};U32 address{};
};
void same(const Result& x,const Result& y){
    CHECK(x.cpu.r==y.cpu.r && x.cpu.eip==y.cpu.eip && x.cpu.flags==y.cpu.flags && x.cpu.defined_flags==y.cpu.defined_flags);
    CHECK(x.cpu.fp.bytes==y.cpu.fp.bytes && x.cpu.selectors==y.cpu.selectors);
    CHECK(x.cpu.fs_base==y.cpu.fs_base && x.cpu.gs_base==y.cpu.gs_base && x.cpu.fs_valid==y.cpu.fs_valid && x.cpu.gs_valid==y.cpu.gs_valid);
    CHECK(x.fault==y.fault && x.kind==y.kind && x.address==y.address);
    CHECK(x.calls==y.calls && x.data==y.data && x.stack==y.stack && x.globals==y.globals);
}

int main(int argc,char** argv){try{
    CHECK(argc==2);
    reference_functions.assign(s::functions().begin(),s::functions().end());
    auto& hot=reference_functions[s::locate(0x43cb90).function-s::functions().data()];
    CHECK(hot.entry==0x43cb90);hot.body=serialized;
    unsigned cases{},faults{},draws{},clear_calls{};
    for(bool linear:{false,true}){
        wr::ProcessOptions options;options.prefer_linear_memory=linear;
        s::Runtime runtime(options);auto& p=runtime.process();
        std::vector<Call> calls;unsigned failure{};
        std::vector<U32> methods;
        for(unsigned slot=0;slot<std::size(methods_device);++slot){const auto method=methods_device[slot];
            p.register_api("special-memory-device",method.name,method.arguments,[&,name=std::string(method.name)](wr::Process& q){
                const auto args=q.argument_snapshot();Call call{name,{args.begin(),args.end()},{}};
                if(name=="DrawPrimitiveUP"){
                    CHECK((args[1]==5 || args[1]==6) && args[2]<=32 && (args[4]==20 || args[4]==32 || args[4]==40));
                    call.vertices.resize((args[2]+2)*args[4]);q.memory.copy_out(args[3],call.vertices);
                }
                calls.push_back(std::move(call));
                if(failure==4 && name=="SetRenderState"){
                    q.cpu.r[wr::EAX]=0x12345678;q.cpu.fp.put16(0,0x37f);
                    throw wr::GuestFault(wr::FaultKind::unsupported,q.cpu.eip,"constructed external prefix");
                }
                return 0u;
            });
            methods.push_back(p.resolve("special-memory-device",method.name));
            if(wa2::graphics::direct_device_method(slot))p.certify_leaf_api("special-memory-device",method.name);
        }
        const auto image=s::initial_image();p.load(image);
        const auto data=p.allocate_bytes(65536),device=data,table=data+0x100,sprite=data+0x1000,resource=data+0x2000;
        p.memory.store(device,table,32);
        for(unsigned n=0;n<methods.size();++n)p.memory.store(table+n*4,methods[n],32);
        p.memory.store(0xb31378,device,32);p.memory.store(0xb30f44,1,32);
        p.memory.store(0xb30f40,resource,32);p.memory.store(0xb3137c,resource,32);
        p.memory.store(0xbc54f0,1,32);
        auto base=p.cpu;base.r[wr::ESP]-=0x8000;
        const auto stack_at=base.r[wr::ESP]-0x4000;
        const auto& section=*std::find_if(image.sections.begin(),image.sections.end(),[](const auto& section){return section.name==".data";});
        const auto globals_at=image.base+section.rva;
        std::vector<std::uint8_t> globals(section.virtual_size),initial_data(65536),initial_stack(0x4040);
        p.memory.copy_out(globals_at,globals);p.memory.copy_out(data,initial_data);
        for(unsigned n=0;n<initial_stack.size();++n)initial_stack[n]=std::uint8_t(n*29+17);
        a::StaticProgram original(p.memory,reference_functions,reference_locate),candidate(p.memory,s::functions(),s::locate);
        const auto f=[&](U32 at,float value){p.memory.store(at,std::bit_cast<U32>(value),32);};
        const auto run=[&](unsigned variant,unsigned control,bool native,bool leaf,unsigned inject=0){
            failure=inject;calls.clear();p.cpu=base;p.cpu.fp.put16(0,std::uint16_t(control));
            p.cpu.flags=0x202|((variant&1)?wr::CF:0);p.cpu.r[wr::EBX]=0x34567891;
            p.memory.copy_in(globals_at,globals);p.memory.copy_in(data,initial_data);p.memory.copy_in(stack_at,initial_stack);
            p.memory.store(sprite+0x15,4,8);p.memory.store(sprite+0x3c,variant%12,32);
            for(unsigned offset:{0x48u,0x4cu})p.memory.store(sprite+offset,0xffff,16);
            const auto mode=(variant/12)%3;
            p.memory.store(sprite+0x4a,mode==0?0xffff:mode==1?0xfffe:0,16);
            p.memory.store(sprite+0x40,variant%3,32);p.memory.store(sprite+0x44,(variant/3)%4,32);
            for(unsigned n=0;n<32;++n)p.memory.store(sprite+0x53+n,(n*29+variant*17)&255,8);
            const float x=-21.25f+variant*.25f,y=73.5f-variant*.5f;
            f(sprite+0x74,variant<36?0.f:float((variant*17)%360));
            f(sprite+0x78,x);f(sprite+0x7c,y);f(sprite+0x80,variant&2?.75f:1);f(sprite+0x84,variant&2?1.25f:1);
            for(unsigned n=0;n<4;++n){f(sprite+0x88+n*4,x+float(n==1 || n==2?300:0));f(sprite+0x98+n*4,y+float(n>=2?200:0));f(sprite+0xac+n*4,1);}
            f(sprite+0xbc,variant&1?19.25f:0);f(sprite+0xc0,variant&1?319.25f:300);
            f(sprite+0xc4,variant&1?23.5f:0);f(sprite+0xc8,variant&1?223.5f:200);
            for(const auto [offset,value]:{std::pair{0x5c,300},{0x5e,200},{0x60,256},{0x62,256},{0x64,2},{0x66,1},{0x68,1}})p.memory.store(resource+offset,value,16);
            p.memory.store(resource,1,8);p.memory.store(resource+0x70,data+0x4000,32);
            for(unsigned n=0;n<64;++n)p.memory.store(data+0x4000+n*4,data+0x8000+n*16,32);
            constexpr U32 returning=0xf0001234;
            p.memory.store(base.r[wr::ESP],returning,32);p.memory.store(base.r[wr::ESP]+4,inject==1?0x21000000:sprite,32);
            p.memory.store(base.r[wr::ESP]+8,mode==2?resource:0,32);p.cpu.eip=0x43cb90;
            if(inject==2)p.cpu.r[wr::ESP]=0;
            if(inject==3)p.memory.store(resource+0x70,0x21000000,32);
            if(inject==5){
                // Two real floating instructions complete before a store
                // fault; the private FP file must commit that exact prefix.
                p.cpu.eip=0x43cf5d;p.cpu.r[wr::ESI]=sprite;p.cpu.r[wr::EBP]=0x21000000+0x1514;
            }
            if(inject==6){
                // A pending, unmasked exception must retain the original ABI.
                p.cpu.eip=0x43cf5d;p.cpu.r[wr::ESI]=sprite;
                p.cpu.fp.put16(0,0x37e);p.cpu.fp.put16(2,0x8081);
            }
            Result result{};
            try{
                for(unsigned n=0;p.cpu.eip!=returning;++n){
                    CHECK(n<8192);std::optional<a::LeafServiceScope> services;
                    if(leaf)services.emplace([](wr::Cpu&,wr::Memory&,void* owner){return static_cast<wr::Process*>(owner)->dispatch_leaf_api();},&p);
                    if(!(native?candidate:original).invoke(p.cpu))CHECK(p.dispatch_api());
                }
            }catch(const wr::GuestFault& fault){result.fault=true;result.kind=fault.kind;result.address=fault.address;}
            result.cpu=p.cpu;result.calls=calls;
            result.data.resize(initial_data.size());p.memory.copy_out(data,result.data);
            result.stack.resize(initial_stack.size());p.memory.copy_out(stack_at,result.stack);
            result.globals.resize(globals.size());p.memory.copy_out(globals_at,result.globals);
            if(!inject)CHECK(!result.fault && result.cpu.eip==returning && result.cpu.r[wr::ESP]==base.r[wr::ESP]+4);
            else CHECK(result.fault);
            return result;
        };
        const auto verify=[&](unsigned variant,unsigned control,bool leaf,unsigned inject=0){
            const auto before=run(variant,control,false,leaf,inject),after=run(variant,control,true,leaf,inject);
            try{same(before,after);}catch(...){std::cerr<<"linear="<<linear<<" variant="<<variant<<" control="<<control<<" leaf="<<leaf<<" inject="<<inject<<'\n';throw;}
            ++cases;if(after.fault)++faults;
            for(const auto& call:after.calls){if(call.name=="DrawPrimitiveUP")++draws;if(call.name=="Clear")++clear_calls;}
        };
        // All blend modes, untextured/clear/textured paths, flipped/scaled and
        // rotated quads, fractional UVs, varying corner colours, both API ABIs.
        for(unsigned variant=0;variant<72;++variant)verify(variant,0x7f,variant&1);
        for(unsigned precision:{0u,2u,3u})for(unsigned rounding=0;rounding<4;++rounding)
            for(unsigned variant:{2u,14u,26u,50u})verify(variant,0x7f|(precision<<8)|(rounding<<10),true);
        for(unsigned inject=1;inject<=6;++inject)for(bool leaf:{false,true})verify(26,0x7f,leaf,inject);
        CHECK(p.executed_instructions==0);
    }
    CHECK(cases==264 && faults==24 && draws>0 && clear_calls>0);
    std::ofstream report(argv[1]);report<<"{\"schema\":\"wa2.special-render-memory.v1\",\"hot_entry\":4443024,\"source_differential_cases\":"<<cases
        <<",\"precise_fault_cases\":"<<faults<<",\"recorded_draws\":"<<draws<<",\"recorded_clear_calls\":"<<clear_calls
        <<",\"exact_cpu_fp_stack_data_globals_and_draw_calls\":true,\"paged_and_linear_memory\":true,\"outer_and_leaf_api_boundaries\":true,\"serialized_source_reference\":true,"
          "\"game_launched\":false,\"screen_access\":false,\"window_or_device_created\":false,\"passed\":true}\n";
    CHECK(report);std::cout<<"PASS "<<cases<<" Special drawing source/state comparisons; no game or device.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
