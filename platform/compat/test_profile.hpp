#pragma once
#include <filesystem>

namespace wa2::platform::compat {
// False performs no filesystem operations. True returns a separate test
// namespace, unlocks its content/reading history, and retains its manual saves.
std::filesystem::path select_test_profile(const std::filesystem::path& normal_root,bool enabled);
}
