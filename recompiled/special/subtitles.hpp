#pragma once
#include "../subtitles.hpp"
#include <functional>
#include <string>

namespace wa2::special {
// BGM identity, automatic chapter transition and stop use SC's own proven ABI.
// Timers, decoded catalogue, geometry and presentation are the common engine.
class SubtitleState {
    std::shared_ptr<subtitles::Playback> player_;
    std::uint32_t bgm_{},chapter_{};
    std::function<std::uint64_t()> clock_;
    std::uint64_t starts_{},changes_{},stops_{};
public:
    SubtitleState(std::shared_ptr<subtitles::Playback>,std::function<std::uint64_t()> clock={});
    void returned(std::uint32_t,const wr::Cpu&,wr::Memory&,std::uint32_t arguments);
    std::string report()const;
};
}
