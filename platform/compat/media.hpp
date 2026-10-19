#pragma once
#include "../../engine/movie_player.hpp"
#include "../../port/audio.hpp"
#include "../render/movie_presenter.hpp"
#include <functional>
namespace wr {class Process;}
namespace wa2::platform::compat {
struct MovieServices {
    std::function<std::int64_t()> monotonic_us;
    std::function<std::shared_ptr<resource::Source>(wr::Process&,std::u16string_view)> open;
    std::function<movie::Information(const std::shared_ptr<resource::Source>&)> inspect;
    std::function<std::unique_ptr<movie::Playback>(std::shared_ptr<resource::Source>,std::shared_ptr<audio::Mixer>)> player;
};
void install_media(wr::Process&,wr::SharedAudio,std::shared_ptr<graphics::MoviePresenter>,MovieServices={});
}
