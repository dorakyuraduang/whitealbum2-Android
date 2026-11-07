#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace wa2::game {
// Application preferences are independent of scene saves and system progress.
// Text speed is the native dialogue budget per logic tick; auto_ticks is a
// delay in 60 Hz logic ticks. Neither is a display-refresh-rate setting.
struct Preferences {
    std::int32_t master{195},music{134},effects{195},speech{195};
    std::int32_t skin{},window_alpha{207},picture_alpha{134},button_alpha{134},novel_alpha{134};
    std::int32_t text_step{2},auto_ticks{137};
    bool window{true},skip_read_only{true},mute_adult{},voice_enabled{true},captions{true};
    // Original character switch order: 0,2,1,3,4,5,10,11,99,98.
    std::array<bool,10> character_voice{true,true,true,true,true,true,true,true,true,true};
    // Muting retains the volume slider and live playback position. Speech's
    // switch remains voice_enabled, including its existing new-line policy.
    std::array<bool,3> audio_muted{};
    bool fullscreen{},confirmation{true},cut_voice_on_advance{},transition_wait{true};
    std::int32_t right_click{};
    // Cached high/low movie modes select external suffixes 0/1 respectively.
    // Changing quality affects the next clip, never restarts an active clip.
    std::int32_t movie_quality{};
    bool operator==(const Preferences&)const=default;
    void validate()const;
    float movie_amplitude()const;
    std::int32_t effective_master()const noexcept{return audio_muted[0]?0:master;}
    std::int32_t effective_music()const noexcept{return audio_muted[1]?0:music;}
    std::int32_t effective_effects()const noexcept{return audio_muted[2]?0:effects;}
    std::int32_t transition_frames(std::int32_t frames,bool skipping=false)const noexcept;
    std::vector<std::uint8_t> encode()const;
    static Preferences decode(std::span<const std::uint8_t>);
};
class PreferenceStorage {
public:
    virtual ~PreferenceStorage()=default;
    virtual std::optional<std::vector<std::uint8_t>> read()=0;
    virtual void replace(std::span<const std::uint8_t>)=0;
};
class PreferenceStore {
    std::shared_ptr<PreferenceStorage> storage_;
    Preferences current_;
public:
    explicit PreferenceStore(std::shared_ptr<PreferenceStorage>);
    const Preferences& current()const noexcept{return current_;}
    // Persist before publishing. Failed writes keep the currently applied
    // value; a menu can report the error and retry the same edit.
    bool replace(const Preferences&);
};
}
