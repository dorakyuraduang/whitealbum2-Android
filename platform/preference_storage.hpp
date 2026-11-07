#pragma once
#include "preferences.hpp"
#include "resource_archive.hpp"
#include "legacy_settings_path.hpp"
#include <filesystem>

namespace wa2::platform {
using LegacySettingsOpener=std::function<std::shared_ptr<resource::Source>(std::string_view)>;
std::shared_ptr<game::PreferenceStorage> preference_storage(const std::filesystem::path& private_directory,LegacySettingsOpener={});
}
