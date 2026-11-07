#pragma once
#include "saved_scene.hpp"
#include "raster_image.hpp"
#include <memory>

namespace wa2::game {
struct SaveDate {
    int year{1970},month{1},day{1},weekday{4},hour{},minute{},second{};
    void validate()const;
    bool operator==(const SaveDate&)const=default;
};
struct SavePreview {
    SaveDate date;
    std::uint64_t saved_ms{};
    std::string scene,description;
    std::shared_ptr<const image::Raster> thumbnail;
    bool legacy{};
    void validate()const;
    std::vector<std::uint8_t> encode()const;
    static SavePreview decode(std::span<const std::uint8_t>);
};
// A separately checksummed small preview precedes the original native scene
// envelope. Browsing ten slots never needs to read their scene payloads.
struct SaveDocument {
    static constexpr std::size_t maximum_preview=128u*1024;
    static constexpr std::size_t prefix_bytes=12+maximum_preview;
    SavePreview preview;
    SavedScene::Bytes snapshot;
    SavedScene::Bytes encode()const;
    static SaveDocument decode(std::span<const std::uint8_t>);
    static SavePreview inspect(std::span<const std::uint8_t> prefix);
    static bool wrapped(std::span<const std::uint8_t>)noexcept;
};
}
