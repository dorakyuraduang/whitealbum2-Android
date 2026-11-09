#pragma once
#include <cstddef>
#include <span>
#include <string>
#include <string_view>

namespace wa2::resource {
// Empty selections preserve the existing root/IC/patch resource order. These
// are original-format PAKs, including their encoded text and atlas ordering.
struct LanguageResources {
    std::string script, font;
    bool chinese_subtitles{true};
};
std::string language_archive_name(std::string_view);
void validate_language_resources(const LanguageResources&);
// Common launch options; each OS entry only supplies its argument strings.
bool language_launch_option(std::span<const std::string_view>,std::size_t& at,LanguageResources&);
}
