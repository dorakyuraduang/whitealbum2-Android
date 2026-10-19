#pragma once
#include "resource_archive.hpp"
#include <cstdio>
#include <filesystem>
#include <optional>

namespace wa2::platform {
// Uses 64-bit C++ stream offsets on every target; paths retain Unicode on Windows.
std::shared_ptr<resource::Source> open_resource_file(const std::filesystem::path& path);
// Only the legacy file-handle adapter needs a shared stream cursor.
bool seek_stream(std::FILE* stream,std::uint64_t offset) noexcept;
std::optional<std::uint64_t> stream_position(std::FILE* stream) noexcept;
}
