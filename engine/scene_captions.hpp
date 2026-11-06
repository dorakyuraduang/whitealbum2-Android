#pragma once
#include "scene_sprite.hpp"
#include "stream_voice.hpp"
#include "subtitles.hpp"
#include <array>

namespace wa2::script {
// Optional main-story captions driven by typed audio events. Borrowed voices
// are removed by their owning controller's stop event before destruction.
class SceneCaptions {
    struct Pending {const audio::Voice* voice{};std::uint32_t key{};std::uint64_t sequence{};bool replace{};};
    std::array<Pending,20> pending_;
    subtitles::Playback playback_;
    std::uint64_t last_wall_{},logical_ms_{},sequence_{},layout_builds_{};
    bool clock_started_{},paused_{};
    std::shared_ptr<const image::Raster> font_;
    const subtitles::Cue* baked_{};
    std::vector<image::Sprite> sprites_;
    static unsigned index(subtitles::Kind,std::uint32_t slot);
    void clock(std::uint64_t now);
public:
    bool queue(std::uint32_t key,subtitles::Kind,std::uint32_t slot,const audio::Voice&,bool replace=false);
    void stop(subtitles::Kind,std::uint32_t slot)noexcept;
    void reset()noexcept;
    void update(std::uint64_t wall_ms);
    void pause(bool value,std::uint64_t wall_ms){clock(wall_ms);paused_=value;}
    const subtitles::Cue* current()noexcept{return playback_.current(logical_ms_);}
    std::span<const image::Sprite> sprites();
    std::uint64_t layout_builds()const noexcept{return layout_builds_;}
};
}
