#pragma once
#include <cstdint>
#include <functional>
namespace wr {class Process;}
namespace wa2::platform::compat {
// Logical source threads use compiled functions. Device notification callbacks
// signal ordinary C++ events; no Win32 handle or OS thread ABI crosses here.
// The shared game runtime may identify one optional idle wait. OS/device
// scheduling contains no source addresses or game-mode decisions.
using IdleSleepPolicy=std::function<bool(std::uint32_t milliseconds,std::uint32_t caller)>;
void install_scheduler(wr::Process&,IdleSleepPolicy={});
}
