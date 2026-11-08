#pragma once
#include "game_directory.hpp"
#include "save_slots.hpp"
#include <filesystem>

namespace wa2::platform {
std::shared_ptr<game::SaveStorage> save_storage(const std::filesystem::path& selected_directory);
std::shared_ptr<game::SaveStorage> save_storage(std::shared_ptr<GameDirectory>);
}
