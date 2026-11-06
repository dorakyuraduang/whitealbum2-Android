#pragma once
#include "save_slots.hpp"
#include <filesystem>

namespace wa2::platform {
std::shared_ptr<game::SaveStorage> save_storage(const std::filesystem::path& private_directory);
}
