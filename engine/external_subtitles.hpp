#pragma once
#include "subtitles.hpp"
#include <functional>
#include <memory>
#include <string>

namespace wa2::subtitles {
inline constexpr std::size_t maximum_yaml_bytes=1024*1024;
struct TextCue {std::uint32_t begin{},end{};std::u32string text;};
struct TextTrack {std::uint32_t key{};std::vector<TextCue> cues;};
struct Document {std::vector<TextTrack> tracks;std::vector<char32_t> characters;std::size_t cue_count{},text_count{};};
// Strict literal subset of the existing main-story sub.yaml. No YAML object,
// include, alias, file reference, executable value or extra group is accepted.
Document parse_yaml(std::span<const std::uint8_t>);
struct GlyphMask {int left{},top{};unsigned width{},height{};std::vector<std::uint8_t> coverage;};
using GlyphRasterizer=std::function<std::vector<GlyphMask>(std::span<const char32_t>)>;
struct Atlas {FontSpec spec;FontImage image;};
Atlas compose_atlas(std::span<const GlyphMask>);
class Catalogue {
    friend std::shared_ptr<const Catalogue> compile_external(const Document&,const GlyphRasterizer&);
    std::vector<std::uint16_t> glyphs_;
    std::vector<Cue> cues_;
    std::vector<Track> tracks_;
    std::unique_ptr<Atlas> atlas_;
    std::size_t glyph_count_{};
    Catalogue()=default;
public:
    Catalogue(const Catalogue&)=delete;
    Catalogue& operator=(const Catalogue&)=delete;
    std::span<const Track> tracks()const noexcept{return tracks_;}
    const Track* find(std::uint32_t)const noexcept;
    FontSpec font()const noexcept;
    std::size_t glyph_count()const noexcept{return glyph_count_;}
    const FontImage* atlas()const noexcept{return atlas_?&atlas_->image:nullptr;}
};
// The compiled embedded glyphs are reused whenever they cover this text. Only
// additional characters require the host's off-screen font rasterizer.
std::shared_ptr<const Catalogue> compile_external(const Document&,const GlyphRasterizer& = {});
}
