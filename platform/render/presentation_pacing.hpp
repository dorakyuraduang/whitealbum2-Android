#pragma once
#include <cstdint>
#include <optional>

namespace wa2::graphics {
// Only coalesce final window submissions. Every source/offscreen draw still
// executes, so readback, partial redraws and resource lifetime stay intact.
class PresentationPacing {
    int source_interval_{},last_requested_{};
    bool accelerated_{},have_submission_{};
    std::uint64_t last_submission_{};
public:
    static constexpr std::uint64_t minimum_interval_ns=8333334; // at most 120 Hz
    struct Decision {bool submit;std::optional<int> swap_interval;};
    void reset(int source_interval){
        source_interval_=last_requested_=source_interval;
        accelerated_=have_submission_=false;last_submission_=0;
    }
    Decision plan(bool accelerated,std::uint64_t now,bool force=false){
        const bool entering=accelerated && !accelerated_;accelerated_=accelerated;
        const int desired=accelerated?0:source_interval_;
        std::optional<int> change;
        // Remember the request even if a driver rejects it: never retry an
        // unsupported interval on every frame. Restore the source request.
        if(desired!=last_requested_){change=desired;last_requested_=desired;}
        if(!accelerated){have_submission_=false;return {true,change};}
        return {entering || force || !have_submission_ || now<last_submission_ ||
            now-last_submission_>=minimum_interval_ns,change};
    }
    void submitted(std::uint64_t now){last_submission_=now;have_submission_=true;}
};
}
