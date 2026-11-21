#pragma once
#include "../engine/external_subtitles.hpp"
#include "../engine/language_resources.hpp"
#include <filesystem>

namespace wa2::platform {
std::shared_ptr<const subtitles::Catalogue> special_subtitle_files(const std::filesystem::path& root,resource::GameLanguage language=resource::GameLanguage::chinese,const subtitles::GlyphRasterizer& rasterizer={});
}
