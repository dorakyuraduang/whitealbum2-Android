#include "frame_metrics.hpp"
#include <algorithm>

namespace wa2::android {
FrameMetrics::FrameMetrics(std::int64_t update_ns) noexcept
    :update_ns_(std::max<std::int64_t>(1,update_ns)){}
void FrameMetrics::reset() noexcept {
    last_=window_=-1;maximum_=0;total_=count_=0;samples_=next_=0;
}
std::optional<FrameSnapshot> FrameMetrics::presented(std::int64_t now) noexcept {
    if(now<0)return std::nullopt;
    if(last_>=0 && now<last_)reset();
    ++total_;
    if(last_<0){last_=window_=now;return std::nullopt;}
    const auto delta=now-last_;last_=now;
    intervals_[next_]=delta;next_=(next_+1)%capacity;
    samples_=std::min(samples_+1,capacity);++count_;
    maximum_=std::max(maximum_,delta);
    const auto elapsed=now-window_;
    if(elapsed<update_ns_)return std::nullopt;
    std::array<std::int64_t,capacity> ordered;
    std::copy_n(intervals_.begin(),samples_,ordered.begin());
    const auto rank=(samples_*95+99)/100-1;
    std::nth_element(ordered.begin(),ordered.begin()+rank,ordered.begin()+samples_);
    const FrameSnapshot snapshot{
        double(count_)*1e9/double(elapsed),double(elapsed)/double(count_)/1e6,
        double(ordered[rank])/1e6,double(maximum_)/1e6,total_,count_,samples_};
    window_=now;maximum_=0;count_=0;samples_=next_=0;
    return snapshot;
}
}
