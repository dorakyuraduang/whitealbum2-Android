#pragma once
#include "../engine/language_resources.hpp"
#include <cstdint>
#include <span>
#include <string_view>
namespace wr {class Process;}
namespace wa2::aot {
struct MenuPrompt {
    std::uint32_t address;
    std::string_view chinese,japanese,english,ukrainian;
};
std::span<const MenuPrompt> language_menu_prompts()noexcept;
// Initialize bounded read-only text data once, before any game execution.
// Game instructions, glyph tables, resource files and save payloads stay intact.
void configure_language_prompts(wr::Process&,resource::GameLanguage);
}

