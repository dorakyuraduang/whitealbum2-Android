#pragma once
#include "../engine/external_subtitles.hpp"
#include <filesystem>
namespace wa2::platform {
std::vector<subtitles::GlyphMask> subtitle_glyphs(std::span<const char32_t>);
std::shared_ptr<const subtitles::Catalogue> subtitle_file(const std::filesystem::path&);
}
