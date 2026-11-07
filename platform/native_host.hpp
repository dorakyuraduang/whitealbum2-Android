#pragma once
#include "native_application.hpp"
#include "frame_driver.hpp"

namespace wa2::platform {
struct NativeHost {
    resource::BankOpener resources;
    LegacySettingsOpener legacy_settings;
    std::function<std::filesystem::path()> private_directory;
    NativeStartup startup;
    game::DriverSettings driver;
    bool fullscreen{};
    std::function<void()> submitted;
    void(*menu_status)(game::UserMenuState)noexcept{};
};
// Shared SDL application entry. OS wrappers supply paths/resource access and
// final error reporting; neither wrapper has its own story or frame policy.
// This entry is compiled/linked by the verifier, never launched there.
#if defined(_WIN32) && defined(WA2_NATIVE_ENTRY_AUDIT)
__declspec(dllexport)
#endif
int run_native_host(NativeHost);
}
