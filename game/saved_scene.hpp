#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace wa2::game {
enum class ScenePart : unsigned {script,reading,controls,choices,selection,calendar,shake,weather,background,bitmaps,characters,colour,dialogue,backlog,music,effects,voices,count};
inline constexpr std::array<std::string_view,unsigned(ScenePart::count)> scene_part_tags{"LSSN","LSRH","LSCT","LSCH","LSSE","LSCL","LSSH","LSWE","LSBG","LSBM","LSAC","LSCO","LSDI","LSBH","LSMR","LSER","LSVR"};
struct SavedScene {
    using Bytes=std::vector<std::uint8_t>;
    std::int64_t logical_us{};
    std::uint32_t random{},command{};
    std::uint64_t ticks{},messages{},voices{};
    std::string scene;
    bool background_changing{},preserve_characters{},window_visible{true},skip_read_only{true},replay{};
    std::array<Bytes,unsigned(ScenePart::count)> parts;
    Bytes& part(ScenePart p){return parts.at(unsigned(p));}
    const Bytes& part(ScenePart p)const{return parts.at(unsigned(p));}
    Bytes encode()const;
    // Checks all envelopes, identity/order and bounds before opening resources.
    // Individual subsystem field semantics are checked by Scene::restore.
    static SavedScene decode(std::span<const std::uint8_t>);
};
}
