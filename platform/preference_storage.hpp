#pragma once
#include "preferences.hpp"
#include <filesystem>

namespace wa2::platform {
std::shared_ptr<game::PreferenceStorage> preference_storage(const std::filesystem::path& private_directory);
}
