#pragma once
#include "application.hpp"

namespace wa2::game {
// Input can arrive between logic ticks. A queued click retains its own hit
// position even if a later pointer-motion event updates the hover position.
class FrameInput {
    Input held_,edges_;
public:
    void add(const Input&);
    Input take();
    void clear()noexcept{held_={};edges_={};}
};
struct DriverSettings {
    unsigned presentation_hz{120},maximum_catch_up{8};
    FrameBudget skip;
    void validate()const;
};
struct DriverInput {Input game;bool background{},quit{};};
struct DriverFrame {
    FrameBatch batch{script::RunState::yielded,0};
    unsigned updates{};
    std::uint64_t dropped_updates{};
    std::int64_t sleep_us{};
    bool draw{},suspended{},closed{};
};
// The same wall-clock/logic/presentation policy is used by every host. A
// presentation tick never advances normal script/animation logic by itself.
// Device ownership, event polling, sleeping and rendering remain adapters.
class FrameDriver {
    Application& application_;
    DriverSettings settings_;
    FrameInput pending_;
    std::int64_t observed_{};
    std::uint64_t logic_phase_{},present_phase_{};
    std::uint64_t clock_revision_{};
    bool started_{},suspended_{};
public:
    explicit FrameDriver(Application&,DriverSettings={});
    DriverFrame pump(std::int64_t now,DriverInput,const std::function<std::int64_t()>& budget_clock);
};
}
