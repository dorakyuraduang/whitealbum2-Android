#pragma once
#include <cstddef>
#include <span>
#include <string>
#include <string_view>

namespace wa2::resource {
enum class GameLanguage {chinese,japanese,english,ukrainian};
enum class ResourceTitle {main_story,special_contents};
struct LanguageProfile {std::string_view tag,script,font,ui;};
const LanguageProfile& language_profile(GameLanguage,ResourceTitle=ResourceTitle::main_story);
GameLanguage parse_language(std::string_view);
// A language selects one verified resource pair and matching default names.
struct LanguageResources {
    GameLanguage language{GameLanguage::chinese};
    bool chinese_subtitles{true};
};
void validate_language_resources(const LanguageResources&,ResourceTitle=ResourceTitle::main_story);
// Common launch options; each OS entry only supplies its argument strings.
bool language_launch_option(std::span<const std::string_view>,std::size_t& at,LanguageResources&);
}
