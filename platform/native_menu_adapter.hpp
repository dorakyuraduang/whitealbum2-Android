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
    virtual void publish(game::UserMenuState,bool alt_activation)=0;
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

enum class MenuActivation {hide,show,focus};
// Default source menus follow the pointer. Alt-only menus wait for the
// pointer to enter before treating a later exit as dismissal. No OS polling
// or game policy is part of this finite presentation state.
class NativeMenuActivation {
    bool mode_{},alt_{},open_{},entered_{};
public:
    void dismiss()noexcept{open_=entered_=false;}
    MenuActivation update(bool alt_mode,bool eligible,bool top_edge,bool alt_down,bool tracking)noexcept{
        const bool pressed=alt_down && !alt_;alt_=alt_down;
        if(mode_!=alt_mode){mode_=alt_mode;dismiss();}
        if(!eligible){dismiss();return MenuActivation::hide;}
        if(tracking)return MenuActivation::show;
        if(!mode_)return top_edge?MenuActivation::show:MenuActivation::hide;
        if(pressed){open_=true;entered_=top_edge;return MenuActivation::focus;}
        if(open_){if(top_edge)entered_=true;else if(entered_)dismiss();}
        return open_?MenuActivation::show:MenuActivation::hide;
    }
};
}
