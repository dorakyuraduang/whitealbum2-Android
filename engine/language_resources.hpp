#pragma once
#include <cstddef>
#include <span>
#include <string>
#include <string_view>

namespace wa2::resource {
enum class GameLanguage {chinese,japanese};
struct LanguageProfile {std::string_view tag,script,font;};
const LanguageProfile& language_profile(GameLanguage);
GameLanguage parse_language(std::string_view);
// A language selects one verified resource pair and matching default names.
struct LanguageResources {
    GameLanguage language{GameLanguage::chinese};
    bool chinese_subtitles{true};
};
void validate_language_resources(const LanguageResources&);
// Common launch options; each OS entry only supplies its argument strings.
bool language_launch_option(std::span<const std::string_view>,std::size_t& at,LanguageResources&);
}
