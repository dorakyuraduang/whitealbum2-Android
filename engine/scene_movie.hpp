#pragma once
#include "movie_player.hpp"
#include "script_runtime.hpp"

namespace wa2::script{
// SetMovie is resumed only after its real decoder reaches playback EOF.
class SceneMovie{
public:
    using Factory=std::function<std::unique_ptr<movie::Playback>(std::string_view,std::int32_t)>;
    explicit SceneMovie(Factory factory,std::function<void()> before_play={},unsigned file_suffix=0);
    std::optional<CommandResult> dispatch(Command&);
    void update(std::int64_t monotonic_us){if(playback_)playback_->update(monotonic_us);}
    void pause(bool value,std::int64_t monotonic_us){if(playback_)playback_->pause(value,monotonic_us);}
    std::optional<movie::VideoFrame> frame()const{return playback_?playback_->frame():std::nullopt;}
    bool active()const{return playback_ && !playback_->finished();}
    const movie::Playback* playback()const noexcept{return playback_.get();}
    void stop(){if(playback_)playback_->stop();}
private:
    Factory factory_;
    std::function<void()> before_play_;
    unsigned suffix_{};
    std::unique_ptr<movie::Playback> playback_;
};
}
