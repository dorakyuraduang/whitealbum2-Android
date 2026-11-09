#pragma once
#include "../engine/language_resources.hpp"
#include <cstdint>
#include <span>
#include <string_view>
namespace wr {class Process;}
namespace wa2::aot {
struct DefaultName {std::uint32_t slot,address;std::string_view chinese,japanese;};
std::span<const DefaultName> language_default_names()noexcept;
// Apply once after initial RAM load and before any game function is executed.
void configure_language_names(wr::Process&,resource::GameLanguage);
}
