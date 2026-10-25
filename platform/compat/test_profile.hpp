#pragma once
#include <filesystem>

namespace wa2::platform::compat {
// False performs no filesystem operations. True returns a separate test
// namespace, seeds only its chapter flags, and retains its manual saves.
std::filesystem::path select_test_profile(const std::filesystem::path& normal_root,bool enabled);
}
