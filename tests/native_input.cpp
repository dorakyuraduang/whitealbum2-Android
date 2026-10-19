#define SDL_MAIN_HANDLED
#include "sdl_input.hpp"
#include <fstream>
#include <iostream>

#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
int main(int argc,char** argv){try{
    if(argc!=2)throw std::runtime_error("usage: wa2_input_tests report.json");
    wa2::platform::SdlInput input(7,[](float x,float y){return std::pair(x*0.5f,y*0.5f);});input.window_size(200,100);unsigned cases=0;
    SDL_Event event{};event.type=SDL_KEYDOWN;event.key.windowID=7;event.key.keysym.scancode=SDL_SCANCODE_LCTRL;input.consume(event);
    CHECK(input.snapshot().keys[SDL_SCANCODE_LCTRL] && input.snapshot().pressed_keys[SDL_SCANCODE_LCTRL]);input.begin_frame();event.key.repeat=1;input.consume(event);
    CHECK(input.snapshot().keys[SDL_SCANCODE_LCTRL] && !input.snapshot().pressed_keys[SDL_SCANCODE_LCTRL]);++cases;
    event={};event.type=SDL_WINDOWEVENT;event.window.windowID=7;event.window.event=SDL_WINDOWEVENT_FOCUS_LOST;input.consume(event);
    CHECK(!input.snapshot().keys.any() && input.snapshot().released_keys[SDL_SCANCODE_LCTRL] && !input.snapshot().focused);++cases;
    input.begin_frame();event={};event.type=SDL_KEYDOWN;event.key.windowID=8;event.key.keysym.scancode=SDL_SCANCODE_A;input.consume(event);CHECK(!input.snapshot().keys.any());++cases;
    event={};event.type=SDL_FINGERDOWN;event.tfinger.windowID=7;event.tfinger.touchId=10;event.tfinger.fingerId=20;event.tfinger.x=0.25f;event.tfinger.y=0.5f;input.consume(event);
    CHECK(input.snapshot().buttons[0] && input.snapshot().pressed_buttons[0] && input.snapshot().x==25 && input.snapshot().y==25);++cases;
    input.begin_frame();event.tfinger.fingerId=21;event.tfinger.x=1;input.consume(event);CHECK(input.snapshot().x==25 && !input.snapshot().pressed_buttons.any());++cases;
    event={};event.type=SDL_MOUSEBUTTONDOWN;event.button.windowID=7;event.button.which=SDL_TOUCH_MOUSEID;event.button.button=1;input.consume(event);
    CHECK(!input.snapshot().pressed_buttons.any());++cases;
    // Physical and touch button sources are combined. Releasing one does not
    // release the other, and SDL's synthesized touch mouse is ignored.
    event.button.which=0;input.consume(event);event={};event.type=SDL_FINGERUP;event.tfinger.windowID=7;event.tfinger.touchId=10;event.tfinger.fingerId=20;input.consume(event);
    CHECK(input.snapshot().buttons[0] && !input.snapshot().released_buttons[0]);
    event={};event.type=SDL_MOUSEBUTTONUP;event.button.windowID=7;event.button.button=1;input.consume(event);CHECK(!input.snapshot().buttons[0] && input.snapshot().released_buttons[0]);++cases;
    input.begin_frame();event={};event.type=SDL_MOUSEWHEEL;event.wheel.windowID=7;event.wheel.preciseX=0.5f;event.wheel.preciseY=2;event.wheel.direction=SDL_MOUSEWHEEL_FLIPPED;input.consume(event);
    CHECK(input.snapshot().wheel_x==-0.5f && input.snapshot().wheel_y==-2);input.begin_frame();CHECK(input.snapshot().wheel_y==0);++cases;
    event={};event.type=SDL_KEYDOWN;event.key.windowID=7;event.key.keysym.scancode=SDL_SCANCODE_SPACE;input.consume(event);
    event={};event.type=SDL_APP_WILLENTERBACKGROUND;input.consume(event);CHECK(input.snapshot().background && !input.snapshot().keys.any() && input.snapshot().released_keys[SDL_SCANCODE_SPACE]);++cases;
    event.type=SDL_APP_DIDENTERFOREGROUND;input.consume(event);CHECK(!input.snapshot().background);++cases;
    event.type=SDL_RENDER_DEVICE_RESET;input.consume(event);CHECK(input.snapshot().renderer_reset);input.begin_frame();CHECK(!input.snapshot().renderer_reset);++cases;
    event.type=SDL_QUIT;input.consume(event);input.begin_frame();CHECK(input.snapshot().quit);++cases;
    wa2::platform::SdlInput logical(7);logical.logical_size(1280,720);logical.window_size(2400,1080);
    event={};event.type=SDL_FINGERDOWN;event.tfinger.windowID=7;event.tfinger.touchId=1;event.tfinger.fingerId=2;event.tfinger.x=.5f;event.tfinger.y=.75f;logical.consume(event);
    CHECK(logical.snapshot().x==640 && logical.snapshot().y==540);++cases;
    event={};event.type=SDL_WINDOWEVENT;event.window.windowID=7;event.window.event=SDL_WINDOWEVENT_SIZE_CHANGED;event.window.data1=1920;event.window.data2=1080;logical.consume(event);
    event={};event.type=SDL_FINGERMOTION;event.tfinger.windowID=7;event.tfinger.touchId=1;event.tfinger.fingerId=2;event.tfinger.x=.25f;event.tfinger.y=.5f;logical.consume(event);
    CHECK(logical.snapshot().x==320 && logical.snapshot().y==360);++cases;
    std::ofstream report(argv[1]);report<<"{\"schema\":\"wa2.native-input.v1\",\"cases\":"<<cases
        <<",\"synthetic_event_structs_only\":true,\"os_event_queue_polled\":false,\"os_input_injected\":false,\"windows_created\":0,\"screen_access\":false,\"game_launched\":false,\"passed\":true}\n";
    report.close();CHECK(report);std::cout<<"PASS native SDL input: "<<cases<<" memory-event cases; no OS input polling/injection or screen access.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
