#pragma once
namespace wa2::android {
// The Android renderer must call this after a successful presentation, never
// from Choreographer or an independent display-refresh callback.
void frame_presented() noexcept;

// Supplied by the actual ARM64 game core. There is deliberately no stub which
// could turn a launcher-only APK into an apparently successful game build.
int run_game(int argc,char** argv);
}
