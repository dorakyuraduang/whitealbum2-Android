#pragma once
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <limits>
#include <sstream>
#include <vector>
namespace wa2::aot {
// Fixed one-record-per-compiled-function storage. Only the first and every
// 127th invocation reads a clock; the report is emitted once on exit/error.
// Native direct callees and certified leaf services are inclusive in a sample.
class FunctionWorkSamples {
public:
    struct Counter {std::uint32_t entry{};std::uint16_t control{};std::uint64_t calls{},samples{},ns{},maximum{};};
    std::vector<Counter> counters;
    std::array<Counter,256> locations{};
    std::uint64_t vertex_grid_attempts{},vertex_grid_completed{};
    std::uint64_t vertex_grid_native_attempts{},vertex_grid_native_completed{};
    std::uint64_t vertex_grid_clipping_attempts{},vertex_grid_clipping_completed{};
    std::uint64_t sprite_geometry_attempts{},sprite_geometry_completed{};
    std::uint64_t snow_motion_attempts{},snow_motion_completed{};
    std::uint64_t string_bulk_attempts{},string_bulk_completed{},string_bulk_bytes{};
    std::uint64_t vector_clear_attempts{},vector_clear_completed{},vector_clear_bytes{};
    std::uint64_t sprite_initialize_attempts{},sprite_initialize_completed{};
    std::uint64_t sprite_argument_attempts{},sprite_argument_completed{};
    std::uint64_t sprite_setter_attempts{},sprite_setter_completed{};
    std::uint64_t weather_batch_attempts{},weather_batch_completed{},weather_batch_yields{};
    std::uint32_t weather_decline_pc{},weather_decline_address{},weather_decline_reason{};
    std::uint64_t weather_color_attempts{},weather_color_completed{},weather_native_operations{};
    std::uint64_t weather_read_ns{},weather_calculate_ns{},weather_write_ns{};
    template<class Functions> explicit FunctionWorkSamples(Functions functions){
        counters.reserve(functions.size());for(const auto& function:functions)counters.push_back({function.entry});
    }
    static Counter* sample(Counter& counter,std::uint16_t control){
        counter.control=control;const auto call=++counter.calls;
        if(call!=1 && call%127)return nullptr;++counter.samples;return &counter;
    }
    Counter* begin(std::size_t index,std::uint16_t control=0){return sample(counters.at(index),control);}
    Counter* begin_location(std::uint32_t owner,std::uint32_t pc,std::uint16_t control){
        if(owner!=0x43b7a0 || !pc)return nullptr;
        const auto first=(pc*0x9e3779b1u)>>24;
        for(unsigned n=0;n<locations.size();++n){auto& slot=locations[(first+n)&255];if(!slot.entry)slot.entry=pc;
            if(slot.entry==pc)return sample(slot,control);}
        return nullptr;
    }
    static void finish(Counter& counter,std::uint64_t ns){counter.ns+=ns;counter.maximum=std::max(counter.maximum,ns);}
    static std::uint64_t estimate(const Counter& counter){
        if(!counter.samples)return 0;const auto mean=counter.ns/counter.samples;
        return counter.calls && mean>std::numeric_limits<std::uint64_t>::max()/counter.calls?std::numeric_limits<std::uint64_t>::max():mean*counter.calls;
    }
    std::string report() const {
        std::vector<const Counter*> ranked;for(const auto& counter:counters)if(counter.calls)ranked.push_back(&counter);
        std::sort(ranked.begin(),ranked.end(),[](const auto* a,const auto* b){return estimate(*a)>estimate(*b);});
        std::ostringstream out;out<<"{\"schema\":\"wa2.sampled-static-functions.v1\",\"sample_interval\":127,\"first_call_sampled\":true,\"timing_inclusive_of_direct_callees_and_leaf_services\":true,\"estimates_are_not_exact_totals\":true,\"hot_functions\":[";
        for(unsigned n=0;n<std::min<std::size_t>(48,ranked.size());++n){if(n)out<<',';const auto& value=*ranked[n];
            out<<"{\"entry\":"<<value.entry<<",\"calls\":"<<value.calls<<",\"samples\":"<<value.samples<<",\"sampled_ns\":"<<value.ns<<",\"max_sample_ns\":"<<value.maximum<<",\"estimated_ns\":"<<estimate(value)<<",\"last_fp_control\":"<<value.control<<'}';}
        ranked.clear();for(const auto& value:locations)if(value.calls)ranked.push_back(&value);
        std::sort(ranked.begin(),ranked.end(),[](const auto* a,const auto* b){return estimate(*a)>estimate(*b);});
        out<<"],\"hot_render_resume_locations\":[";
        for(unsigned n=0;n<std::min<std::size_t>(32,ranked.size());++n){if(n)out<<',';const auto& value=*ranked[n];
            out<<"{\"entry\":"<<value.entry<<",\"calls\":"<<value.calls<<",\"samples\":"<<value.samples<<",\"sampled_ns\":"<<value.ns<<",\"max_sample_ns\":"<<value.maximum<<",\"estimated_ns\":"<<estimate(value)<<",\"last_fp_control\":"<<value.control<<'}';}
        out<<"],\"vertex_grid\":{\"attempts\":"<<vertex_grid_attempts<<",\"completed\":"<<vertex_grid_completed<<",\"native_float_attempts\":"<<vertex_grid_native_attempts<<",\"native_float_completed\":"<<vertex_grid_native_completed<<",\"clipping_attempts\":"<<vertex_grid_clipping_attempts<<",\"clipping_completed\":"<<vertex_grid_clipping_completed<<"},\"snow_motion\":{\"attempts\":"<<snow_motion_attempts<<",\"completed\":"<<snow_motion_completed<<"},\"sprite_geometry\":{\"attempts\":"<<sprite_geometry_attempts<<",\"completed\":"<<sprite_geometry_completed<<"},\"bulk_strings\":{\"attempts\":"<<string_bulk_attempts<<",\"completed\":"<<string_bulk_completed<<",\"bytes\":"<<string_bulk_bytes<<"},\"vector_clear\":{\"attempts\":"<<vector_clear_attempts<<",\"completed\":"<<vector_clear_completed<<",\"bytes\":"<<vector_clear_bytes<<"},\"sprite_initialize\":{\"attempts\":"<<sprite_initialize_attempts<<",\"completed\":"<<sprite_initialize_completed<<"},\"sprite_argument_moves\":{\"attempts\":"<<sprite_argument_attempts<<",\"completed\":"<<sprite_argument_completed<<"},\"sprite_setters\":{\"attempts\":"<<sprite_setter_attempts<<",\"completed\":"<<sprite_setter_completed<<"},\"weather_batch\":{\"attempts\":"<<weather_batch_attempts<<",\"completed\":"<<weather_batch_completed<<",\"yields\":"<<weather_batch_yields<<",\"last_decline_pc\":"<<weather_decline_pc<<",\"last_decline_address\":"<<weather_decline_address<<",\"last_decline_reason\":"<<weather_decline_reason<<",\"color_attempts\":"<<weather_color_attempts<<",\"color_completed\":"<<weather_color_completed<<",\"native_operations\":"<<weather_native_operations<<",\"read_ns\":"<<weather_read_ns<<",\"calculate_ns\":"<<weather_calculate_ns<<",\"write_ns\":"<<weather_write_ns<<"},\"bounded_functions\":"<<counters.size()<<",\"bounded_render_locations\":256,\"screen_access\":false}";return out.str();
    }
};
inline thread_local FunctionWorkSamples* current_work_samples{};
struct FunctionWorkScope {
    FunctionWorkSamples* previous{current_work_samples};
    explicit FunctionWorkScope(FunctionWorkSamples* samples){current_work_samples=samples;}
    ~FunctionWorkScope(){current_work_samples=previous;}
};
struct FunctionWorkTimer {
    FunctionWorkSamples::Counter *counter{},*location{};std::chrono::steady_clock::time_point start;
    explicit FunctionWorkTimer(std::size_t index,std::uint32_t owner=0,std::uint32_t pc=0,std::uint16_t control=0){
        if(current_work_samples){counter=current_work_samples->begin(index,control);location=current_work_samples->begin_location(owner,pc,control);}
        if(counter || location)start=std::chrono::steady_clock::now();}
    ~FunctionWorkTimer(){if(counter || location){const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
        if(counter)FunctionWorkSamples::finish(*counter,ns);if(location)FunctionWorkSamples::finish(*location,ns);}}
};
}
