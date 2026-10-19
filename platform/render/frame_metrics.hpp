#pragma once
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <sstream>
namespace wa2::graphics {
// Counters/timestamps for this renderer only. No pixels, files or per-frame
// report writes; the host serializes the bounded result on exit/error.
struct FrameMetrics {
    bool enabled{true};
    std::uint16_t source_fp_control{};
    std::uint64_t draw{},apply{},flush{},upload{},previous_counter{};
    struct Sample {std::uint64_t frame,counter,begin,draw,apply,flush,upload,copy,present,error,swap,draws,uploads;std::array<std::uint64_t,10> work;};
    std::deque<Sample> recent;
    std::uint64_t last_copy{},last_draws{},last_uploads{},frames{},intervals{},interval_total{},interval_max{};
    std::array<std::uint64_t,7> interval_histogram{},total_stages{};
    std::array<std::uint64_t,10> last_work{};
    static std::uint64_t now(){return std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
    struct Timer {
        std::uint64_t* value{};std::uint64_t start{};
        explicit Timer(std::uint64_t* destination):value(destination){if(value)start=now();}
        ~Timer(){if(value)*value+=now()-start;}
    };
    void presented(std::uint64_t frame,std::uint64_t begin,std::uint64_t before_error,std::uint64_t after_error,
                   std::uint64_t after_swap,std::uint64_t copy,std::uint64_t draws,std::uint64_t uploads,
                   std::array<std::uint64_t,10> work={}){
        const auto copy_delta=copy-last_copy;
        std::array<std::uint64_t,10> work_delta{};for(unsigned n=0;n<work.size();++n)work_delta[n]=work[n]-last_work[n];
        const Sample value{frame,after_swap,begin,draw,apply,flush,upload,copy_delta,before_error-begin,
            after_error-before_error,after_swap-after_error,draws-last_draws,uploads-last_uploads,work_delta};
        if(recent.size()==256)recent.pop_front();recent.push_back(value);++frames;
        const std::array<std::uint64_t,7> stages{draw,apply,flush,upload,copy_delta,value.error,value.swap};
        for(unsigned n=0;n<stages.size();++n)total_stages[n]+=stages[n];
        if(previous_counter){const auto elapsed=after_swap-previous_counter;++intervals;interval_total+=elapsed;interval_max=std::max(interval_max,elapsed);
            const std::array<std::uint64_t,6> limits{8333333,16666667,25000000,33333333,50000000,100000000};
            unsigned bucket{};while(bucket<limits.size() && elapsed>limits[bucket])++bucket;++interval_histogram[bucket];}
        previous_counter=after_swap;last_copy=copy;last_draws=draws;last_uploads=uploads;draw=apply=flush=upload=0;
        last_work=work;
    }
    std::string report() const {
        const auto anchor=now();std::ostringstream out;
        const auto array=[&](const auto& values){out<<'[';bool first=true;for(const auto value:values){if(!first)out<<',';first=false;out<<value;}out<<']';};
        out<<"{\"enabled\":true,\"counter_anchor\":"<<anchor<<",\"steady_anchor_ns\":"<<anchor<<",\"counter_frequency\":1000000000,\"frame_columns\":[\"frame\",\"counter\",\"present_begin_counter\",\"draw_ns\",\"apply_ns\",\"flush_ns\",\"upload_ns\",\"surface_copy_ns\",\"present_ns\",\"error_check_ns\",\"swap_ns\",\"draws\",\"uploads\"],\"recent\":[";
        bool first=true;for(const auto& v:recent){if(!first)out<<',';first=false;array(std::array{v.frame,v.counter,v.begin,v.draw,v.apply,v.flush,v.upload,v.copy,v.present,v.error,v.swap,v.draws,v.uploads});}
        out<<"],\"frames\":"<<frames<<",\"intervals\":"<<intervals<<",\"interval_total_ns\":"<<interval_total<<",\"interval_max_ns\":"<<interval_max
            <<",\"interval_histogram_upper_ms\":[8.333333,16.666667,25,33.333333,50,100,null],\"interval_histogram\":";array(interval_histogram);
        out<<",\"stage_total_columns\":[\"draw_inclusive\",\"apply_inclusive\",\"flush\",\"upload\",\"surface_copy\",\"error_check\",\"swap\"],\"stage_total_ns\":";array(total_stages);
        out<<",\"work_columns\":[\"frame\",\"static_function_entries\",\"api_calls\",\"direct_leaf_api_calls\",\"main_sleep_wall_ns\",\"main_wait_wall_ns\",\"main_sleep_requested_ms\",\"main_sleep_zero_calls\",\"function_boundary_wall_ns\",\"api_boundary_wall_ns\",\"poll_wall_ns\"],\"work_times_are_inclusive\":true,\"source_fp_control_at_present\":"<<source_fp_control<<",\"recent_work\":[";
        first=true;for(const auto& value:recent){if(!first)out<<',';first=false;out<<'['<<value.frame;for(const auto count:value.work)out<<','<<count;out<<']';}out<<']';
        out<<",\"bounded_recent_frames\":256,\"screen_capture\":false}";return out.str();
    }
};
}
