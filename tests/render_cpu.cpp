// Bounded source-function replay against synthetic RAM and a CPU-only device.
// No startup, window, graphics context, resource file, or frame loop is run.
#include "process.hpp"
#include "native_math.hpp"
#include "uv_clip.hpp"
#include "fp_registers.hpp"
#include "gpr_registers.hpp"
#include "work_samples.hpp"
#include "geometry_cache.hpp"
#include "../platform/render/submission_cache.hpp"
#include <winrecomp/win32_state.hpp>
#include <bit>
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <cmath>
enum class Kind {unknown,d3d,device,texture,surface,vertexbuffer,indexbuffer,vertexshader,pixelshader,declaration,stateblock,swapchain,buffer,base_texture,resource};
#include "../port/d3d9_tables.inc"
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
#include "portrait_submission_snapshots.hpp"
namespace a=wa2::aot;
namespace wa2::aot {extern thread_local bool native_grid_float_enabled;extern thread_local bool native_grid_registers_enabled;extern thread_local bool native_sprite_geometry_enabled;}
#ifdef _WIN32
extern "C" __declspec(dllimport) void* __stdcall GetCurrentThread();
extern "C" __declspec(dllimport) int __stdcall QueryThreadCycleTime(void*,unsigned long long*);
#endif
std::uint64_t component_cycles(){
#ifdef _WIN32
    unsigned long long value{};CHECK(QueryThreadCycleTime(GetCurrentThread(),&value));return value;
#else
    return 0;
#endif
}
int main(int argc,char** argv){try{
    CHECK(argc>=2 && argc<=16);bool capture_texture_batches{};bool pc24{},dense{},reference{},serialized{},masked{},varied{},grid_reference{},grid_software{},compare_grid{},compare_services{},outer_services{},compare_all{},compare_geometry{};
    for(int n=2;n<argc;++n){const std::string option(argv[n]);if(option=="--texture-batches"){capture_texture_batches=true;continue;}if(option=="--pc24")pc24=true;else if(option=="--dense")dense=true;else if(option=="--reference")reference=true;
        else if(option=="--serialized")serialized=true;else if(option=="--masked")masked=true;else if(option=="--varied")varied=true;else if(option=="--grid-reference")grid_reference=true;else if(option=="--grid-software")grid_software=true;else if(option=="--compare-grid")compare_grid=true;else if(option=="--compare-services")compare_services=true;else if(option=="--outer-services")outer_services=true;else if(option=="--compare-all")compare_all=true;else if(option=="--compare-geometry")compare_geometry=true;else CHECK(false);}
    a::native_grid_float_enabled=!grid_software;
    std::optional<a::NativeRenderMathReferenceScope> reference_scope;if(reference)reference_scope.emplace();
    std::optional<a::fp::RegisterFpReferenceScope> serialized_scope;if(serialized)serialized_scope.emplace();
    std::optional<a::NativeVertexGridReferenceScope> grid_reference_scope;if(grid_reference)grid_reference_scope.emplace();
    const auto glyph_cache_before=a::geometry_cache_counts();a::Runtime runtime;auto& p=runtime.process();
    a::FunctionWorkSamples work(a::functions());a::FunctionWorkScope work_scope(&work);
    std::map<std::string,unsigned> calls;PortraitSubmission portrait;
    std::vector<std::vector<std::uint8_t>> vertices;
    std::vector<wr::U32> device_methods;
    for(unsigned slot=0;slot<std::size(methods_device);++slot){const auto& method=methods_device[slot];
        p.register_api("cpu-device",method.name,method.arguments,[&,name=std::string(method.name)](wr::Process& q){
            ++calls[name];const auto args=q.argument_snapshot();portrait.method(name,args);
            if(name=="DrawPrimitiveUP"){
                if(!((args[1]==5 || args[1]==6) && args[2]<=32 && (args[4]==20 || args[4]==32 || args[4]==40))){for(auto arg:args)std::cerr<<arg<<' ';std::cerr<<'\n';throw std::runtime_error("unexpected synthetic primitive");}
                auto& value=vertices.emplace_back((args[2]+2)*args[4]);q.memory.copy_out(args[3],value);
                for(unsigned at=0;at<value.size();at+=args[4])for(unsigned xy:{0u,4u}){float coordinate;std::memcpy(&coordinate,value.data()+at+xy,4);CHECK(std::isfinite(coordinate));}
                portrait.draw(args[1],args[2]+2,value,args[4]);
            }
            return 0u;
        });
        device_methods.push_back(p.resolve("cpu-device",method.name));
        if(wa2::graphics::direct_device_method(slot))p.certify_leaf_api("cpu-device",method.name);
    }
    runtime.load();
    const auto data=p.allocate_bytes(65536),device=data,table=data+0x100,sprite=data+0x1000,resource=data+0x2000;
    p.memory.store(device,table,32);
    for(unsigned i=0;i<device_methods.size();++i)p.memory.store(table+i*4,device_methods[i],32);
    p.memory.store(0xb34438,device,32);p.memory.store(0xb34004,1,32);
    p.memory.store(0xb34000,resource,32);p.memory.store(0xb3443c,resource,32);
    p.memory.store(0xbc830c,1,32);
    p.memory.store(sprite+0x3c,1,32);p.memory.store(sprite+0x48,0xffff,16);
    p.memory.store(sprite+0x15,4,8);
    p.memory.store(sprite+0x4a,0xffff,16);
    p.memory.store(sprite+0x4c,0xffff,16);
    const auto f=[&](wr::U32 at,float v){p.memory.store(at,std::bit_cast<wr::U32>(v),32);};
    f(sprite+0x78,20);f(sprite+0x7c,30);f(sprite+0x84,1);f(sprite+0x88,dense?1940:300);f(sprite+0x98,dense?1110:200);f(sprite+0xa8,1);
    f(sprite+0xbc,20);f(sprite+0xc0,dense?1940:300);f(sprite+0xc4,30);f(sprite+0xc8,dense?1110:200);
    if(varied){
        // Complete four corners and both scale axes, plus different colours
        // at each corner. This exercises real interpolation instead of the
        // old fixture's collapsed X axis and four all-zero colour channels.
        f(sprite+0x80,1);f(sprite+0x84,1);
        // X and Y are separate four-value arrays, not interleaved pairs.
        const float right=dense?1940.f:300.f,bottom=dense?1110.f:200.f;
        for(unsigned n=0;n<4;++n){f(sprite+0x88+n*4,(n==1 || n==2)?right:20.f);f(sprite+0x98+n*4,n<2?30.f:bottom);}
        for(unsigned n=0;n<32;++n)p.memory.store(sprite+0x53+n,(n*29+17)&255,8);
    }
    if(capture_texture_batches)for(unsigned corner=0;corner<4;++corner)p.memory.store(sprite+0x53+corner*4,0xff000000u,32);
    // These fields describe each tile; the independent grid below describes
    // the complete image. Keep tile size fixed when covering a larger grid.
    p.memory.store(resource+0x5c,dense?1920:300,16);p.memory.store(resource+0x5e,dense?1080:200,16);
    p.memory.store(resource+0x60,256,16);p.memory.store(resource+0x62,256,16);
    p.memory.store(resource,1,8);p.memory.store(resource+0x68,1,16);
    p.memory.store(resource+0x64,dense?8:2,16);p.memory.store(resource+0x66,dense?5:2,16);
    p.memory.store(resource+0x70,data+0x4000,32);
    for(unsigned i=0;i<64;++i)p.memory.store(data+0x4000+i*4,capture_texture_batches?resource+0x8000+i*16:resource,32);
    if(masked){std::array<std::uint8_t,128> record{};p.memory.copy_out(resource,record);p.memory.copy_in(0xa93fb8,record);p.memory.copy_in(0xa94038,record);}
    p.callback(p.resolve("kernel32.dll","GetProcessHeap"),{});
    auto base=p.cpu;base.r[wr::ESP]-=0x8000;if(pc24)base.fp.put16(0,0x7f);
    struct Result {wr::Cpu cpu;std::vector<std::vector<std::uint8_t>> vertices;std::map<std::string,unsigned> calls;std::vector<std::uint8_t> stack;unsigned function_entries{},outer_apis{},direct_apis{};};
    a::Program program(p.memory);
    bool collect_work{};std::uint64_t function_ns{},api_ns{},lookup_ns{},function_entries{},api_entries{};
    std::map<wr::U32,std::array<std::uint64_t,2>> resume_work;
    const auto run=[&](unsigned mode,bool metered,bool services=true){
        calls.clear();vertices.clear();p.cpu=base;p.cpu.eip=0x43b7a0;p.set_error(0);
        p.memory.store(sprite+0x4c,mode==2 && masked?1:0xffff,16);
        const wr::U32 returning=0xf0001234;p.memory.store(base.r[wr::ESP],returning,32);
        p.memory.store(base.r[wr::ESP]+4,sprite,32);p.memory.store(base.r[wr::ESP]+8,mode==2?resource:0,32);
        std::uint64_t budget=200000;unsigned boundaries{},entry_count{},outer_apis{};a::NativeMathReferenceScope original_numeric_body;
        const auto direct_before=p.direct_leaf_api_calls;
        while(p.cpu.eip!=returning){CHECK(++boundaries<=4096);
            const auto entry=p.cpu.eip;const auto begin=collect_work?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
            std::optional<a::LeafServiceScope> leaf;
            if(!metered && services && !outer_services)leaf.emplace([](wr::Cpu& cpu,wr::Memory& memory,void* owner){auto& process=*static_cast<wr::Process*>(owner);CHECK(&cpu==&process.cpu && &memory==&process.memory);return process.dispatch_leaf_api();},&p);
            const auto known=metered?program.invoke(p.cpu,budget):program.invoke_unmetered(p.cpu);leaf.reset();
            if(known)++entry_count;
            if(collect_work){const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-begin).count();if(known){function_ns+=ns;++function_entries;auto& row=resume_work[entry];++row[0];row[1]+=ns;}else lookup_ns+=ns;}
            if(!known){const auto begin=collect_work?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
                CHECK(p.dispatch_api());++outer_apis;if(collect_work){api_ns+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-begin).count();++api_entries;}}
        }
        Result result{p.cpu,vertices,calls,std::vector<std::uint8_t>(0x2040)};
        result.function_entries=entry_count;result.outer_apis=outer_apis;result.direct_apis=unsigned(p.direct_leaf_api_calls-direct_before);
        p.memory.copy_out(base.r[wr::ESP]-0x2000,result.stack);return result;
    };
    if(capture_texture_batches){CHECK(dense && !masked);p.memory.store(sprite+0x4a,0,16);portrait.active=true;run(2,false);portrait.check();portrait.active=false;}
    std::ofstream report(argv[1]);report<<"{\"source_texture_batch_replay\":{\"enabled\":"<<(capture_texture_batches?"true":"false")
        <<",\"distinct_source_tiles\":"<<(capture_texture_batches?"true":"false")<<",\"source_draws\":"<<portrait.draws<<",\"accepted_draws\":"<<portrait.accepted
        <<",\"recorded_driver_packets\":"<<portrait.packets<<",\"same_production_packet_builder\":true,\"unchanged_texture_uv_colour_position_order\":true},\"source_fp_control\":"<<base.fp.get16(0)<<",\"dense_texture_grid\":"<<(dense?"true":"false")<<",\"native_render_math\":"<<(reference?"false":"true")
        <<",\"register_floating_point\":"<<(serialized?"false":"true")<<",\"masked_texture\":"<<(masked?"true":"false")<<",\"nondegenerate_gradient_quad\":"<<(varied?"true":"false")<<",\"cases\":[";
    for(unsigned mode=0;mode<3;++mode){
        p.memory.store(sprite+0x4a,mode==0?0xffff:mode==1?0xfffe:0,16);
        const auto apis=p.api_calls;
        const auto start=std::chrono::steady_clock::now();
        const auto checked=run(mode,true),native=run(mode,false),ordinary=run(mode,false,false);
        Result original_registers;{a::NativeIntegerReferenceScope reference;original_registers=run(mode,false);}
        CHECK(checked.cpu.r==native.cpu.r && checked.cpu.eip==native.cpu.eip && checked.cpu.flags==native.cpu.flags && checked.cpu.defined_flags==native.cpu.defined_flags);
        if(checked.cpu.fp.bytes!=native.cpu.fp.bytes || checked.stack!=native.stack){
            std::cerr<<"snapshot mode "<<mode<<" control "<<base.fp.get16(0)<<'\n';unsigned shown{};
            for(unsigned i=0;i<checked.cpu.fp.bytes.size() && shown<16;++i)if(checked.cpu.fp.bytes[i]!=native.cpu.fp.bytes[i]){std::cerr<<"FP "<<i<<' '<<unsigned(checked.cpu.fp.bytes[i])<<' '<<unsigned(native.cpu.fp.bytes[i])<<'\n';++shown;}
            shown=0;for(unsigned i=0;i<checked.stack.size() && shown<16;++i)if(checked.stack[i]!=native.stack[i]){std::cerr<<"stack "<<i<<' '<<unsigned(checked.stack[i])<<' '<<unsigned(native.stack[i])<<'\n';++shown;}
        }
        CHECK(checked.cpu.fp.bytes==native.cpu.fp.bytes && checked.stack==native.stack);
        CHECK(checked.vertices==native.vertices && checked.calls==native.calls);
        CHECK(ordinary.cpu.r==native.cpu.r && ordinary.cpu.eip==native.cpu.eip && ordinary.cpu.flags==native.cpu.flags && ordinary.cpu.defined_flags==native.cpu.defined_flags);
        CHECK(ordinary.cpu.fp.bytes==native.cpu.fp.bytes && ordinary.stack==native.stack && ordinary.vertices==native.vertices && ordinary.calls==native.calls);
        CHECK(original_registers.cpu.r==native.cpu.r && original_registers.cpu.eip==native.cpu.eip && original_registers.cpu.flags==native.cpu.flags && original_registers.cpu.defined_flags==native.cpu.defined_flags && original_registers.cpu.fp.bytes==native.cpu.fp.bytes);
        CHECK(original_registers.stack==native.stack && original_registers.vertices==native.vertices && original_registers.calls==native.calls);
        a::native_grid_registers_enabled=false;const auto original_grid_registers=run(mode,false);a::native_grid_registers_enabled=true;
        CHECK(original_grid_registers.cpu.r==native.cpu.r && original_grid_registers.cpu.eip==native.cpu.eip && original_grid_registers.cpu.flags==native.cpu.flags && original_grid_registers.cpu.defined_flags==native.cpu.defined_flags && original_grid_registers.cpu.fp.bytes==native.cpu.fp.bytes);
        CHECK(original_grid_registers.stack==native.stack && original_grid_registers.vertices==native.vertices && original_grid_registers.calls==native.calls);
        a::native_sprite_geometry_enabled=false;const auto original_geometry=run(mode,false);a::native_sprite_geometry_enabled=true;
        CHECK(original_geometry.cpu.r==native.cpu.r && original_geometry.cpu.eip==native.cpu.eip && original_geometry.cpu.flags==native.cpu.flags && original_geometry.cpu.defined_flags==native.cpu.defined_flags && original_geometry.cpu.fp.bytes==native.cpu.fp.bytes);
        CHECK(original_geometry.stack==native.stack && original_geometry.vertices==native.vertices && original_geometry.calls==native.calls);
        for(const auto* other:{&checked,&ordinary})CHECK(other->cpu.selectors==native.cpu.selectors && other->cpu.fs_base==native.cpu.fs_base && other->cpu.gs_base==native.cpu.gs_base && other->cpu.fs_valid==native.cpu.fs_valid && other->cpu.gs_valid==native.cpu.gs_valid);
        if(!outer_services){CHECK(native.function_entries<ordinary.function_entries && native.outer_apis<ordinary.outer_apis && native.direct_apis>0);}
        CHECK(mode==1?native.calls.at("Clear")==1:!native.vertices.empty());
        if(dense && mode==2 && !masked)CHECK(native.calls.at("DrawPrimitiveUP")>=12);
        const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
        if(mode)report<<',';report<<"{\"mode\":"<<mode<<",\"check_ns\":"<<ns<<",\"api_calls\":"<<p.api_calls-apis
            <<",\"outer_function_entries_before\":"<<ordinary.function_entries<<",\"outer_function_entries_after\":"<<native.function_entries
            <<",\"outer_api_boundaries_before\":"<<ordinary.outer_apis<<",\"outer_api_boundaries_after\":"<<native.outer_apis<<",\"direct_device_calls\":"<<native.direct_apis
            <<",\"exact_cpu_stack_and_vertices\":true,\"device_calls\":{";
        bool comma=false;for(const auto& [name,count]:calls){if(comma)report<<',';comma=true;report<<wr::quote(name)<<':'<<count;}report<<"}}";
    }
    // Fixed component work, not a frame loop: repeat the same three small
    // source-function inputs twenty times each. Keep validation and all ABI
    // boundaries in the timed path, so compiler changes cannot remove work.
    std::array<std::uint64_t,3> duration{};unsigned replay_draws{};
    constexpr unsigned rounds=5,repetitions=4;
    for(unsigned round=0;round<rounds;++round)for(unsigned order=0;order<3;++order){
        const auto mode=(round+order)%3;
        p.memory.store(sprite+0x4a,mode==0?0xffff:mode==1?0xfffe:0,16);
        const auto start=std::chrono::steady_clock::now();
        for(unsigned n=0;n<repetitions;++n){const auto result=run(mode,false);
            CHECK(mode==1?result.calls.at("Clear")==1:!result.vertices.empty());
            replay_draws+=unsigned(result.vertices.size());}
        duration[mode]+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
    }
    collect_work=true;for(unsigned mode=0;mode<3;++mode){p.memory.store(sprite+0x4a,mode==0?0xffff:mode==1?0xfffe:0,16);for(unsigned n=0;n<4;++n)run(mode,false);}collect_work=false;
    std::array<std::uint64_t,3> runtime_duration{};
    for(unsigned mode=0;mode<3;++mode){
        p.memory.store(sprite+0x4a,mode==0?0xffff:mode==1?0xfffe:0,16);
        p.memory.store(sprite+0x4c,mode==2 && masked?1:0xffff,16);
        const auto start=std::chrono::steady_clock::now();
        for(unsigned n=0;n<4;++n){calls.clear();vertices.clear();p.cpu=base;p.cpu.r[wr::ESP]+=12;
            a::NativeMathReferenceScope original_numeric_body;
            p.callback(0x43b7a0,std::array<wr::U32,2>{sprite,mode==2?resource:0},false);
            CHECK(mode==1?calls.at("Clear")==1:!vertices.empty());}
        runtime_duration[mode]=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
    }
    report<<"],\"paired_grid_calls_per_sample\":16,\"paired_grid_ns\":[";
    if(compare_grid){
        // Compare inside ONE process with the same RAM/device/allocations.
        // Alternate order, with fixed 16-call samples and no frame/GPU loop.
        a::FunctionWorkScope quiet(nullptr);const auto previous=a::native_vertex_grid_enabled;
        p.memory.store(sprite+0x4a,0,16);
        for(unsigned pair=0;pair<8;++pair){std::array<std::uint64_t,2> elapsed{};
            for(unsigned order=0;order<2;++order){const auto after=(pair+order)&1;a::native_vertex_grid_enabled=after;
                const auto start=std::chrono::steady_clock::now();for(unsigned n=0;n<16;++n){const auto result=run(2,false);CHECK(!result.vertices.empty());}
                elapsed[after]=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();}
            if(pair)report<<',';report<<'['<<elapsed[0]<<','<<elapsed[1]<<']';}
        a::native_vertex_grid_enabled=previous;
    }
    report<<"],\"paired_services_calls_per_sample\":16,\"paired_services_ns\":[";
    if(compare_services){
        CHECK(!outer_services);a::FunctionWorkScope quiet(nullptr);p.memory.store(sprite+0x4a,0,16);
        for(unsigned pair=0;pair<8;++pair){std::array<std::uint64_t,2> elapsed{};
            for(unsigned order=0;order<2;++order){const auto after=(pair+order)&1;
                const auto start=std::chrono::steady_clock::now();for(unsigned n=0;n<16;++n){const auto result=run(2,false,after);CHECK(!result.vertices.empty());}
                elapsed[after]=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();}
            if(pair)report<<',';report<<'['<<elapsed[0]<<','<<elapsed[1]<<']';}
    }
    report<<"],\"paired_pipeline_calls_per_sample\":16,\"paired_pipeline_ns\":[";
    std::vector<std::array<std::uint64_t,2>> pipeline_cycles;
    if(compare_all){
        CHECK(!outer_services);a::FunctionWorkScope quiet(nullptr);p.memory.store(sprite+0x4a,0,16);
        const auto previous=a::native_integer_registers_enabled;
        for(unsigned pair=0;pair<8;++pair){std::array<std::uint64_t,2> elapsed{},cycles{};
            for(unsigned order=0;order<2;++order){const auto after=(pair+order)&1;a::native_integer_registers_enabled=after;a::native_grid_registers_enabled=after;
                const auto first_cycles=component_cycles();
                const auto start=std::chrono::steady_clock::now();for(unsigned n=0;n<16;++n){const auto result=run(2,false,after);CHECK(!result.vertices.empty());}
                elapsed[after]=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();cycles[after]=component_cycles()-first_cycles;}
            pipeline_cycles.push_back(cycles);if(pair)report<<',';report<<'['<<elapsed[0]<<','<<elapsed[1]<<']';}
        a::native_integer_registers_enabled=previous;
        a::native_grid_registers_enabled=true;
    }
    report<<"],\"paired_pipeline_thread_cycles\":[";for(unsigned n=0;n<pipeline_cycles.size();++n){if(n)report<<',';report<<'['<<pipeline_cycles[n][0]<<','<<pipeline_cycles[n][1]<<']';}
    report<<"],\"paired_geometry_ns\":[";
    if(compare_geometry){
        a::FunctionWorkScope quiet(nullptr);p.memory.store(sprite+0x4a,0,16);
        for(unsigned pair=0;pair<8;++pair){std::array<std::uint64_t,2> elapsed{};
            for(unsigned order=0;order<2;++order){const auto after=(pair+order)&1;a::native_sprite_geometry_enabled=after;
                const auto start=std::chrono::steady_clock::now();for(unsigned n=0;n<16;++n){const auto result=run(2,false);CHECK(!result.vertices.empty());}
                elapsed[after]=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();}
            if(pair)report<<',';report<<'['<<elapsed[0]<<','<<elapsed[1]<<']';}
        a::native_sprite_geometry_enabled=true;
    }
    report<<"],\"sprite_geometry\":{\"attempts\":"<<work.sprite_geometry_attempts<<",\"completed\":"<<work.sprite_geometry_completed<<"},\"resume_work\":[";bool work_comma=false;for(const auto& [entry,row]:resume_work){if(work_comma)report<<',';work_comma=true;report<<'['<<entry<<','<<row[0]<<','<<row[1]<<']';}
    CHECK(reference || masked || grid_reference || work.vertex_grid_completed>0);
    CHECK(!pc24 || !varied || reference || masked || grid_reference || grid_software || work.vertex_grid_native_completed>0);
    report<<"],\"vertex_grid\":{\"attempts\":"<<work.vertex_grid_attempts<<",\"completed\":"<<work.vertex_grid_completed<<",\"native_float_attempts\":"<<work.vertex_grid_native_attempts<<",\"native_float_completed\":"<<work.vertex_grid_native_completed<<",\"clipping_attempts\":"<<work.vertex_grid_clipping_attempts<<",\"clipping_completed\":"<<work.vertex_grid_clipping_completed<<"},\"bounded_replay\":{\"calls_per_mode\":"<<rounds*repetitions<<",\"mode_ns\":["<<duration[0]<<','<<duration[1]<<','<<duration[2]<<"],\"total_draw_submissions\":"<<replay_draws<<"},\"work_breakdown\":{\"snapshot_calls\":12,\"function_ns\":"<<function_ns<<",\"api_ns\":"<<api_ns<<",\"lookup_ns\":"<<lookup_ns<<",\"function_entries\":"<<function_entries<<",\"api_entries\":"<<api_entries<<"},\"runtime_boundary_replay\":{\"calls_per_mode\":4,\"mode_ns\":["<<runtime_duration[0]<<','<<runtime_duration[1]<<','<<runtime_duration[2]<<"]},\"maximum_source_instructions_per_checked_snapshot\":200000,\"maximum_host_boundaries_per_snapshot\":4096,\"whole_game_fps_measured\":false,\"graphics_device_created\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}";
    const auto glyph_cache_after=a::geometry_cache_counts();
    CHECK(glyph_cache_before.grid_hits==glyph_cache_after.grid_hits && glyph_cache_before.grid_misses==glyph_cache_after.grid_misses &&
        glyph_cache_before.sprite_hits==glyph_cache_after.sprite_hits && glyph_cache_before.sprite_misses==glyph_cache_after.sprite_misses);
    std::cout<<"PASS bounded synthetic sprite CPU replay; moving/global sprites bypass glyph memoization.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
