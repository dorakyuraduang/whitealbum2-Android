#pragma once
namespace wr {class Process;}
namespace wa2::platform::compat {
// Logical source threads use compiled functions. Device notification callbacks
// signal ordinary C++ events; no Win32 handle or OS thread ABI crosses here.
void install_scheduler(wr::Process&);
}
