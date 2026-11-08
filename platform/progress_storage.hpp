#pragma once
#include "game_directory.hpp"
#include "../engine/progress.hpp"
#include <filesystem>

namespace wa2::platform {
// The selected game directory must exist. Root Sys.sav is authoritative;
// one matching primary Documents child is imported only when root is absent.
// The separate introductory bank follows the source's masked OR-merge policy.
std::shared_ptr<progress::Storage> progress_storage(const std::filesystem::path& selected_directory);
std::shared_ptr<progress::Storage> progress_storage(std::shared_ptr<GameDirectory>,bool combined=false);
}
