#pragma once
// Extra subtitles for audio-only main-story passages. No OS, CPU, device or
// game-resource dependency; both hosts use these exact immutable data/timers.
#include <cstdint>
#include <span>
#include <vector>

namespace wa2::subtitles {
struct Cue {
    std::uint32_t begin_ms{},end_ms{};
    std::span<const std::uint16_t> glyphs;
};
struct Track {std::uint32_t key{};std::span<const Cue> cues;};
struct FontSpec {std::uint32_t cell{},columns{},rows{},advance{},origin_x{},baseline{};};
std::span<const Track> main_story_tracks() noexcept;
std::span<const char32_t> characters() noexcept;
const Track* find_track(std::uint32_t key) noexcept;
FontSpec font_spec() noexcept;
std::span<const std::uint8_t> font_coverage() noexcept;
struct FontImage {std::uint32_t width{},height{};std::vector<std::uint8_t> rgba;};
FontImage decode_font(std::span<const std::uint8_t>,FontSpec,std::size_t glyphs);
struct GlyphQuad {float left{},top{},right{},bottom{},u0{},v0{},u1{},v1{};};
// Positions use the original subtitle patch's 1280x720 canvas, at y=40.
// Wrap only captions wider than the canvas; presentation scales the same
// geometry to the existing letterboxed game viewport.
std::vector<GlyphQuad> layout(const Cue&,FontSpec,std::size_t glyphs);
enum class Kind {effect,voice};
class Playback {
    const Track* track_{};
    Kind kind_{};
    std::uint32_t slot_{};
    std::uint64_t started_{},generation_{};
public:
    bool play(std::uint32_t key,Kind,std::uint32_t slot,std::uint64_t now,bool replace=false);
    void stop(Kind,std::uint32_t slot) noexcept;
    void reset() noexcept {if(track_){track_=nullptr;++generation_;}}
    const Cue* current(std::uint64_t now) noexcept;
    std::uint64_t generation()const noexcept{return generation_;}
};
}
