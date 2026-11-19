#pragma once
#include <cstdint>
#include <optional>

namespace wa2::graphics {
// Only coalesce final window submissions. Every source/offscreen draw still
// executes, so readback, partial redraws and resource lifetime stay intact.
class PresentationPacing {
    int source_interval_{},last_requested_{};
    bool accelerated_{},have_submission_{},limit_60_{};
    std::uint64_t last_submission_{};
public:
    static constexpr std::uint64_t minimum_interval_ns=8333334; // at most 120 Hz
    static constexpr std::uint64_t interval_60_ns=16666667;
    struct Decision {bool submit;std::optional<int> swap_interval;};
    void reset(int source_interval,bool limit_60=false){
        source_interval_=last_requested_=source_interval;
        accelerated_=have_submission_=false;last_submission_=0;limit_60_=limit_60;
    }
    Decision plan(bool accelerated,std::uint64_t now,bool force=false){
        const bool entering=accelerated && !accelerated_;accelerated_=accelerated;
        const int desired=accelerated?0:source_interval_;
        std::optional<int> change;
        // Remember the request even if a driver rejects it: never retry an
        // unsupported interval on every frame. Restore the source request.
        if(desired!=last_requested_){change=desired;last_requested_=desired;}
        if(!accelerated)return {true,change};
        return {entering || force || !have_submission_ || now<last_submission_ ||
            now-last_submission_>=(limit_60_?interval_60_ns:minimum_interval_ns),change};
    }
    // Only wait for the unused part of this frame. A slow render or a
    // blocking display swap already pays towards the interval.
    std::uint64_t wait_after_swap(std::uint64_t now)const{
        if(!limit_60_ || !have_submission_ || now<last_submission_)return 0;
        const auto elapsed=now-last_submission_;
        return elapsed<interval_60_ns?interval_60_ns-elapsed:0;
    }
    void submitted(std::uint64_t now){last_submission_=now;have_submission_=true;}
};
}
