#pragma once
#include "sdl_input.hpp"
#include <memory>
#include <utility>

struct SDL_Window;
namespace wa2::platform {
// Presentation-only services. Policy and command execution remain in the
// common Application; an OS menu never reads or changes a scene.
class NativeMenuAdapter {
public:
    virtual ~NativeMenuAdapter()=default;
    virtual void publish(game::UserMenuState)=0;
    virtual void filter(InputSnapshot&)=0;
};
// An OS menu can run a nested message loop inside SDL_PollEvent. Suspend its
// owner immediately, then expose one background pump before resumption. That
// pump discards the elapsed gap while retaining explicit selected commands.
class NativeMenuGap {
    bool pending_{};
public:
    void enter(const std::function<void()>& suspend){if(!suspend)throw std::invalid_argument("Missing native menu suspension service");pending_=true;suspend();}
    bool filter(InputSnapshot& input){
        if(!std::exchange(pending_,false))return false;
        input.keys.reset();input.pressed_keys.reset();input.released_keys.reset();
        input.buttons.reset();input.pressed_buttons.reset();input.released_buttons.reset();
        input.pointer=false;input.wheel_x=input.wheel_y=0;input.background=true;return true;
    }
};
using NativeMenuFactory=std::function<std::unique_ptr<NativeMenuAdapter>(SDL_Window*,std::function<void()>)>;
}
