#pragma once
#include <cstdint>
#include <string>
namespace wa2::android {
// The Android renderer must call this after a successful presentation, never
// from Choreographer or an independent display-refresh callback.
void frame_presented() noexcept;
// Optional shared-core diagnostic switch. The independent native target does
// not register an AOT callback or link any recompiled code.
void set_performance_switch(void(*)(bool),std::uint64_t(*)()=nullptr,std::string(*)()=nullptr) noexcept;

// Supplied by the actual ARM64 game core. There is deliberately no stub which
// could turn a launcher-only APK into an apparently successful game build.
int run_game(int argc,char** argv);
}
