#pragma once
#include "raster_image.hpp"
#include <memory>
#include <optional>
#include <string>

namespace wa2::image {
struct AnimationPiece {
    std::uint8_t texture{};
    std::int16_t source_x{},source_y{},width{},height{},x{},y{};
    bool empty()const noexcept{return width==0 || height==0;}
};
struct AnimationFrame {std::vector<AnimationPiece> pieces;};
struct AnimationStep {std::uint16_t operation{},argument{},duration{},event{};};
struct AnimationClip {std::vector<AnimationStep> steps;};
// INAS is the original sprite-animation container, separate from movies.
// Only the neutral textured-frame features used by the existing PAKs are
// implemented. Other transforms/blends/events fail at the resource boundary.
class Animation {
    std::uint32_t rate_{};
    std::uint64_t fingerprint_{};
    std::vector<AnimationFrame> frames_;
    std::vector<AnimationClip> clips_;
    std::vector<std::string> textures_;
public:
    static std::shared_ptr<const Animation> parse(std::span<const std::uint8_t> bytes);
    std::uint32_t rate()const noexcept{return rate_;}
    std::uint64_t fingerprint()const noexcept{return fingerprint_;}
    std::span<const AnimationFrame> frames()const noexcept{return frames_;}
    std::span<const AnimationClip> clips()const noexcept{return clips_;}
    std::span<const std::string> textures()const noexcept{return textures_;}
};
class AnimationPlayer {
    struct Loop {std::uint32_t start{},remaining{};};
    std::shared_ptr<const Animation> data_;
    std::vector<Loop> loops_;
    std::optional<std::uint32_t> frame_;
    std::uint32_t clip_{},cursor_{},elapsed_{},repeat_{},remaining_{},fraction_{},clock_rate_{};
    bool hold_{},finished_{};
    void advance();
public:
    // Repeat 0 is continuous; N>0 is N complete passes of the sequence.
    AnimationPlayer(std::shared_ptr<const Animation>,std::uint32_t clip=0,std::uint32_t repeat=0,bool hold_last=true);
    void tick(std::uint32_t logic_rate=60);
    std::optional<std::uint32_t> frame()const noexcept{return frame_;}
    bool finished()const noexcept{return finished_;}
    std::vector<std::uint8_t> checkpoint()const;
    void restore(std::span<const std::uint8_t> bytes);
};
}
