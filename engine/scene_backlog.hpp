#pragma once
#include "script_runtime.hpp"
#include <deque>

namespace wa2::script {
struct BacklogVoice {std::int32_t speaker{},chapter{},line{},volume{255};bool operator==(const BacklogVoice&)const=default;};
struct BacklogEntry {
    std::string scene,text,name;
    std::int32_t line{-1},speaker{-1};bool novel{},choice{};
    std::vector<BacklogVoice> voices;
};
// The source keeps at most 255 visible entries and ten voice descriptors per
// entry. This native deque retains that order without shifting every string
// and voice table each time a line is added.
class SceneBacklog {
    std::deque<BacklogEntry> entries_;
    std::optional<BacklogVoice> recent_voice_;
public:
    static constexpr std::size_t capacity=255;
    const std::deque<BacklogEntry>& entries()const noexcept{return entries_;}
    void clear()noexcept{entries_.clear();recent_voice_.reset();}
    void note_voice(BacklogVoice);
    void clear_voice()noexcept{recent_voice_.reset();}
    void push(BacklogEntry);
    void message(std::string_view scene,std::int32_t line,std::string_view text,std::string_view name,std::int32_t speaker,bool novel,bool voice_log=true,bool choice=false);
    std::vector<std::uint8_t> checkpoint()const;
    void restore(std::span<const std::uint8_t>);
};
}
