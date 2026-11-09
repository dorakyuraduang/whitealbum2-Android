#pragma once
#include <cstdint>
#include <string>
namespace wa2::window {
// A burst of key/cursor/PeekMessage queries shares one device event pump.
// Queued events stay in SDL/widgets until the next pump, at most 1 ms later.
// Positive-timeout waits and a clock rollback always bypass coalescing.
class EventPump {
    std::uint64_t last_{};bool sampled_{};
    std::uint64_t queries_{},pumps_{},coalesced_{},blocking_{};
public:
    static constexpr std::uint64_t IntervalNs=1000000;
    bool due(unsigned timeout,std::uint64_t now){
        ++queries_;
        if(!timeout && sampled_ && now>=last_ && now-last_<IntervalNs){++coalesced_;return false;}
        sampled_=true;last_=now;++pumps_;blocking_+=bool(timeout);return true;
    }
    void reset(){sampled_=false;}
    std::string report()const{
        return "{\"queries\":"+std::to_string(queries_)+",\"device_pumps\":"+std::to_string(pumps_)+",\"coalesced_queries\":"+std::to_string(coalesced_)+",\"blocking_pumps\":"+std::to_string(blocking_)+",\"maximum_nonblocking_interval_ns\":"+std::to_string(IntervalNs)+",\"screen_access\":false}";
    }
};
}
