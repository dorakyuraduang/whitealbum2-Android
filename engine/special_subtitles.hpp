#pragma once
#include "external_subtitles.hpp"
#include "language_resources.hpp"

namespace wa2::subtitles {
// External SC patch resources only. No DLL, game executable or embedded Main
// track is used. Both OS adapters supply bounded byte ranges to this decoder.
std::shared_ptr<const Catalogue> compile_special_binary(std::span<const std::uint8_t> data,
    std::span<const std::uint8_t> dialogue_font,std::span<const std::uint8_t> song_font);
// Version selection is fixed; adapters never accept an SC subtitle picker URI.
std::string_view special_subtitle_name(resource::GameLanguage);
std::shared_ptr<const Catalogue> compile_special_text(std::span<const std::uint8_t>,resource::GameLanguage,const GlyphRasterizer&);
}
