#pragma once
#include "movie_player.hpp"
#include "script_runtime.hpp"

namespace wa2::script{
// SetMovie owns its transport through EOF or a source-permitted skip. It
// releases the owner when the suspended script command completes.
class SceneMovie{
public:
    using Factory=std::function<std::unique_ptr<movie::Playback>(std::string_view,std::int32_t)>;
    explicit SceneMovie(Factory factory,std::function<void()> before_play={},unsigned file_suffix=0,std::function<void(std::int32_t)> seen={},std::function<bool(std::int32_t)> skip_allowed={});
    ~SceneMovie();
    std::optional<CommandResult> dispatch(Command&);
    void update(std::int64_t monotonic_us){if(playback_)playback_->update(monotonic_us);}
    void pause(bool value,std::int64_t monotonic_us){if(playback_)playback_->pause(value,monotonic_us);}
    std::optional<movie::VideoFrame> frame()const{return playback_?playback_->frame():std::nullopt;}
    bool active()const noexcept{return bool(playback_);}
    void gain(float amplitude);
    void file_suffix(unsigned);
    const movie::Playback* playback()const noexcept{return playback_.get();}
    void stop(){if(playback_){record_seen();playback_->stop();}}
    bool request_skip();
private:
    Factory factory_;
    std::function<void()> before_play_;
    unsigned suffix_{};
    std::function<void(std::int32_t)> seen_;
    std::function<bool(std::int32_t)> skip_allowed_;
    std::int32_t record_{};
    bool recorded_{};
    float amplitude_{1};
    std::unique_ptr<movie::Playback> playback_;
    void record_seen();
};
}
