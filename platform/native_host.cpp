#include "native_host.hpp"
#include "sdl_application.hpp"
#include <SDL.h>
#include <chrono>
#include <thread>

namespace wa2::platform {
namespace {
struct SdlLifetime {
    SdlLifetime(){if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER|SDL_INIT_EVENTS))throw std::runtime_error(SDL_GetError());}
    ~SdlLifetime(){SDL_Quit();}
};
}
int run_native_host(NativeHost host){
    if(!host.resources || !host.storage)throw std::invalid_argument("Missing native host resource or game-directory storage adapter");host.driver.validate();
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,"linear");SdlLifetime sdl;
    const auto directory=host.storage();if(!directory)throw std::invalid_argument("Native host game directory is unavailable");
    std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("WHITE ALBUM2",SDL_WINDOWPOS_UNDEFINED,SDL_WINDOWPOS_UNDEFINED,
        1280,720,SDL_WINDOW_RESIZABLE|SDL_WINDOW_ALLOW_HIGHDPI|(host.fullscreen?SDL_WINDOW_FULLSCREEN_DESKTOP:0)),SDL_DestroyWindow);
    if(!window)throw std::runtime_error(std::string("Create native game window: ")+SDL_GetError());
    SDL_StopTextInput();
    auto canvas=SdlCanvas::create(window.get(),true);canvas->logical_size(1280,720);
    auto application=native_application(std::move(host.resources),directory,host.startup,{},{},std::move(host.legacy_settings),std::move(host.legacy_saves));
    SdlInput input(SDL_GetWindowID(window.get()));input.logical_size(1280,720);
    struct MenuLifetime {decltype(host.menu_status) publish;~MenuLifetime(){if(publish)publish({});}} menu_lifetime{host.menu_status};
    const auto epoch=std::chrono::steady_clock::now();const auto clock=[&]{return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-epoch).count();};
    std::unique_ptr<NativeMenuAdapter> menu;SdlApplication::InputFilter filter;if(host.menu)filter=[&](InputSnapshot& events){if(menu)menu->filter(events);};
    const auto publish=[&]{const auto state=application->user_menu();if(host.menu_status)host.menu_status(state);if(menu)menu->publish(state,application->preferences().menu_alt);};
    SdlApplication runner(*application,*canvas,host.driver,{},[&]{publish();if(host.submitted)host.submitted();},std::move(filter));
    if(host.menu){menu=host.menu(window.get(),[&]{application->background(true,clock());});if(!menu)throw std::runtime_error("Native host menu adapter is unavailable");}publish();
    runner.run(input,clock,
        [](auto microseconds){std::this_thread::sleep_for(std::chrono::microseconds(microseconds));});
    return 0;
}
}
