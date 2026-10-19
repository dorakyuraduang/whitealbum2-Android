#pragma once
#include "resource_archive.hpp"
#include <string_view>

namespace wa2::android {
// Opens an original PAK in the user-selected directory, without importing it
// into app storage. Keep the returned Source/Archive alive while it is in use.
std::shared_ptr<resource::Source> open_resource_bank(std::string_view filename);
std::shared_ptr<resource::Source> try_open_resource_bank(std::string_view filename);
void report_game_failure(std::string_view message) noexcept;
}
