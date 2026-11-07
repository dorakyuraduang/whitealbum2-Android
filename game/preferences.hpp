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
    std::int32_t text_step{10},auto_ticks{137};
    bool window{true},skip_read_only{true},mute_adult{},voice_enabled{true},captions{true};
    // Original character switch order: 0,2,1,3,4,5,10,11,99,98.
    std::array<bool,10> character_voice{true,true,true,true,true,true,true,true,true,true};
    bool operator==(const Preferences&)const=default;
    void validate()const;
    float movie_amplitude()const;
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
