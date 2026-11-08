#pragma once
#include "resource_archive.hpp"
#include <filesystem>
#include <optional>

namespace wa2::platform {
struct GameFileInfo {std::string name;std::uint64_t bytes{},modified_ms{};std::string diagnostic;};
// Paths and save formats are common policy. OS adapters access only the
// currently selected game directory (a SAF tree on Android).
bool game_file_path(std::string_view name,bool writable=false) noexcept;
std::optional<unsigned> game_slot_file(std::string_view name) noexcept;
class GameDirectory {
public:
    virtual ~GameDirectory()=default;
    virtual std::shared_ptr<resource::Source> open(std::string_view name)=0;
    virtual void replace(std::string_view name,std::span<const std::uint8_t>)=0;
    // Preserve other bytes; the file must already have expected_size bytes.
    virtual void update(std::string_view name,std::uint64_t expected_size,std::uint64_t offset,std::span<const std::uint8_t>)=0;
    virtual std::vector<GameFileInfo> saves(std::string_view directory="native-saves")=0;
};
std::shared_ptr<GameDirectory> game_directory(const std::filesystem::path& selected_directory);
std::shared_ptr<GameDirectory> game_test_directory(std::shared_ptr<GameDirectory>);
std::optional<std::vector<std::uint8_t>> game_file_read(GameDirectory&,std::string_view,std::uint64_t minimum,std::uint64_t maximum,std::optional<std::size_t> prefix={});
}
