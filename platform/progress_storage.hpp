#pragma once
#include "../engine/progress.hpp"
#include <filesystem>

namespace wa2::platform {
// The host supplies a private save directory, never the PAK resource root.
// The directory must exist; only its Sys.sav file is opened or written.
std::shared_ptr<progress::Storage> progress_storage(const std::filesystem::path& private_directory);
}
