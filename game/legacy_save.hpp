#pragma once
#include "save_document.hpp"

namespace wa2::game {
// Header inspection and audio conversion are separate from loading a complete
// original scene. The caller supplies owned bytes; these routines open no files,
// publish no progress and create no playback transports.
struct LegacySaveHeader {
    std::uint32_t version{},story_code{},millisecond{};
    SaveDate date;
    std::string description;
};
inline constexpr std::size_t original_save_header_bytes=88;
inline constexpr std::size_t original_scene_v2_bytes=0x2434;
LegacySaveHeader inspect_legacy_save_header(std::span<const std::uint8_t>);
bool original_manual_save(std::span<const std::uint8_t>)noexcept;
SavePreview inspect_legacy_save_preview(std::span<const std::uint8_t>);
struct LegacyAudioSave {SavedScene::Bytes music,effects,voices;};
LegacyAudioSave import_legacy_scene_audio(std::span<const std::uint8_t>);
}
