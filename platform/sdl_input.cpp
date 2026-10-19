#include "sdl_input.hpp"
#include <SDL.h>
#include <cmath>
#include <stdexcept>

namespace wa2::platform {
SdlInput::SdlInput(std::uint32_t window_id,Coordinates coordinates)
    :window_id_(window_id),coordinates_(std::move(coordinates)){}
void SdlInput::enter() const {if(std::this_thread::get_id()!=owner_)throw std::runtime_error("native SDL input called from a foreign thread");}
bool SdlInput::accepts(std::uint32_t id) const noexcept {return !window_id_ || !id || id==window_id_;}
void SdlInput::window_size(int width,int height){enter();if(!logical_extent_ && width>0 && height>0){width_=width;height_=height;}}
void SdlInput::logical_size(int width,int height){enter();if(width<1 || height<1)throw std::invalid_argument("invalid logical input size");width_=width;height_=height;logical_extent_=true;}
void SdlInput::begin_frame(){
    enter();snapshot_.pressed_keys.reset();snapshot_.released_keys.reset();snapshot_.pressed_buttons.reset();snapshot_.released_buttons.reset();
    snapshot_.wheel_x=snapshot_.wheel_y=0;snapshot_.renderer_reset=false;
}
void SdlInput::position(float x,float y){
    if(coordinates_){const auto point=coordinates_(x,y);x=point.first;y=point.second;}
    if(std::isfinite(x) && std::isfinite(y)){snapshot_.x=x;snapshot_.y=y;}
}
void SdlInput::button(unsigned index){
    const auto down=mouse_buttons_.at(index)||(index==0 && finger_.has_value());
    if(down!=snapshot_.buttons[index]){
        (down?snapshot_.pressed_buttons:snapshot_.released_buttons).set(index);snapshot_.buttons.set(index,down);
    }
}
void SdlInput::release_all(){
    snapshot_.released_keys|=snapshot_.keys;snapshot_.keys.reset();mouse_buttons_.fill(false);finger_.reset();
    for(unsigned i=0;i<5;++i)button(i);
}
void SdlInput::consume(const SDL_Event& event){
    enter();
    switch(event.type){
    case SDL_QUIT:snapshot_.quit=true;release_all();break;
    case SDL_APP_WILLENTERBACKGROUND:case SDL_APP_DIDENTERBACKGROUND:snapshot_.background=true;release_all();break;
    case SDL_APP_WILLENTERFOREGROUND:case SDL_APP_DIDENTERFOREGROUND:snapshot_.background=false;break;
    case SDL_RENDER_DEVICE_RESET:case SDL_RENDER_TARGETS_RESET:snapshot_.renderer_reset=true;break;
    case SDL_WINDOWEVENT:
        if(!accepts(event.window.windowID))break;
        switch(event.window.event){
        case SDL_WINDOWEVENT_SIZE_CHANGED:case SDL_WINDOWEVENT_RESIZED:window_size(event.window.data1,event.window.data2);break;
        case SDL_WINDOWEVENT_FOCUS_GAINED:snapshot_.focused=true;break;
        case SDL_WINDOWEVENT_FOCUS_LOST:snapshot_.focused=false;release_all();break;
        case SDL_WINDOWEVENT_MINIMIZED:snapshot_.minimized=true;release_all();break;
        case SDL_WINDOWEVENT_RESTORED:case SDL_WINDOWEVENT_SHOWN:snapshot_.minimized=false;break;
        case SDL_WINDOWEVENT_CLOSE:snapshot_.quit=true;release_all();break;
        default:break;
        }break;
    case SDL_KEYDOWN:case SDL_KEYUP:{
        if(!accepts(event.key.windowID))break;
        const auto key=int(event.key.keysym.scancode);if(key<0 || key>=SDL_NUM_SCANCODES)break;
        const bool down=event.type==SDL_KEYDOWN;
        if(snapshot_.keys[std::size_t(key)]!=down){
            (down?snapshot_.pressed_keys:snapshot_.released_keys).set(std::size_t(key));snapshot_.keys.set(std::size_t(key),down);
        }break;
    }
    case SDL_MOUSEMOTION:
        if(accepts(event.motion.windowID) && event.motion.which!=SDL_TOUCH_MOUSEID && !finger_)position(float(event.motion.x),float(event.motion.y));break;
    case SDL_MOUSEBUTTONDOWN:case SDL_MOUSEBUTTONUP:{
        if(!accepts(event.button.windowID) || event.button.which==SDL_TOUCH_MOUSEID)break;
        if(!finger_)position(float(event.button.x),float(event.button.y));
        const auto index=unsigned(event.button.button)-1;
        if(index<5){mouse_buttons_[index]=event.type==SDL_MOUSEBUTTONDOWN;button(index);}break;
    }
    case SDL_MOUSEWHEEL:
        if(accepts(event.wheel.windowID) && event.wheel.which!=SDL_TOUCH_MOUSEID){
            const auto direction=event.wheel.direction==SDL_MOUSEWHEEL_FLIPPED?-1.0f:1.0f;
            snapshot_.wheel_x+=event.wheel.preciseX*direction;snapshot_.wheel_y+=event.wheel.preciseY*direction;
        }break;
    case SDL_FINGERDOWN:case SDL_FINGERMOTION:case SDL_FINGERUP:{
        if(!accepts(event.tfinger.windowID))break;
        const auto id=std::pair(event.tfinger.touchId,event.tfinger.fingerId);
        if(event.type==SDL_FINGERDOWN && !finger_)finger_=id;
        if(finger_!=id)break;
        position(event.tfinger.x*float(width_),event.tfinger.y*float(height_));
        if(event.type==SDL_FINGERUP)finger_.reset();button(0);break;
    }
    default:break;
    }
}
void SdlInput::poll(){enter();SDL_Event event;while(SDL_PollEvent(&event))consume(event);}
}
