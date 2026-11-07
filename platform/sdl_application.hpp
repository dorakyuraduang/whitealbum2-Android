#pragma once
#include "audio_binding.hpp"
#include "frame_driver.hpp"
#include "sdl_input.hpp"
#include "sdl_presentation.hpp"

namespace wa2::platform {
game::Input game_input(const InputSnapshot&);
// One production pump/loop for both hosts. Window/resource/private-storage
// creation stays in the host; SDL events, rendering and device audio are
// adapters around the common Application and FrameDriver.
class SdlApplication {
    game::Application& application_;
    game::FrameDriver driver_;
    SdlCanvas& canvas_;
    SdlPresentation presentation_;
    AudioBinding audio_;
    game::RenderFrame frame_;
    std::function<void()> submitted_;
public:
    using Clock=std::function<std::int64_t()>;
    using Sleep=std::function<void(std::int64_t)>;
    SdlApplication(game::Application&,SdlCanvas&,game::DriverSettings={},AudioOutputFactory={},std::function<void()> submitted={});
    game::DriverFrame pump(std::int64_t now,const InputSnapshot&,const Clock& budget_clock);
    void run(SdlInput&,const Clock&,const Sleep&);
};
}
