#pragma once
#include "application.hpp"
#include "preference_storage.hpp"
#include "legacy_slots.hpp"
#include <chrono>
#include <filesystem>

namespace wa2::platform {
struct NativeStartup {
    bool test_mode{};
    std::uint32_t seed{1};
    game::TitleStartup title;
    std::optional<game::TitleContent> content;
    std::chrono::milliseconds image_timeout{30000};
};
// Arguments exclude argv[0]. This policy is common to desktop and Android.
NativeStartup native_startup(std::span<const std::string_view> arguments);
// The host supplies an existing private directory, separate from external
// resources and the retained AOT application's files. No PAK is copied.
std::unique_ptr<game::Application> native_application(resource::BankOpener,const std::filesystem::path& private_directory,
    NativeStartup={},game::SceneServices={},game::TitleServices={},LegacySettingsOpener={},game::LegacySaveSources={});
}
