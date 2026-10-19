#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
namespace wr {class Process;}
namespace wa2::aot {
struct MovieSettings {
    std::size_t groups{};
    unsigned available_variants{};
    std::optional<std::uint32_t> previous,selected;
    bool changed{};
    std::string reason;
    std::string profile_file;
    std::string report() const;
};
// Shared startup configuration for the preserved mov_lv filename branch.
// Reads directory metadata only; no movie extraction or CPU-speed guess.
MovieSettings configure_movie_resources(wr::Process&,const std::filesystem::path& profile_file);
void prepare_movie_profile(wr::Process&,const std::filesystem::path&,std::string_view section,std::string_view key);
}
