#pragma once
#include "native_application.hpp"

namespace wa2::platform {
struct NativeDesktopStartup {NativeStartup game;std::optional<std::filesystem::path> resources;};
std::filesystem::path native_utf8_path(std::string_view);
NativeDesktopStartup native_desktop_startup(std::span<const std::string_view>);
// Discovery examines directory/file metadata only. Original executable names
// and image bytes do not participate in selecting an external resource root.
std::filesystem::path native_desktop_directory(const std::filesystem::path& executable_directory,
    const std::filesystem::path& working_directory,const std::optional<std::filesystem::path>& selected={});
resource::BankOpener native_desktop_banks(const std::filesystem::path&);
LegacySettingsOpener native_desktop_legacy_settings(const std::filesystem::path&);
game::LegacySaveSources native_desktop_legacy_saves(const std::filesystem::path&);
}
