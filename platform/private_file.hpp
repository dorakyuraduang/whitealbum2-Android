#pragma once
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace wa2::platform::private_file {
void require_directory(const std::filesystem::path&);
std::optional<std::uint64_t> size(const std::filesystem::path&,std::uint64_t minimum=0,std::uint64_t maximum=std::numeric_limits<std::uint64_t>::max());
std::optional<std::vector<std::uint8_t>> read(const std::filesystem::path&,std::uint64_t minimum,std::uint64_t maximum);
// The caller owns the containing private directory and selects the filename.
// Never creates parent directories, follows links, or truncates a published
// file. Only its exclusively created temporary may be cleaned up on failure.
void replace(const std::filesystem::path&,std::span<const std::uint8_t>,std::uint64_t minimum,std::uint64_t maximum);
}
