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
    if(!host.resources || !host.private_directory)throw std::invalid_argument("Missing native host resource or private-directory adapter");host.driver.validate();
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,"linear");SdlLifetime sdl;
    const auto directory=host.private_directory();if(directory.empty() || !directory.is_absolute())throw std::invalid_argument("Native host private directory must be absolute");
    std::filesystem::create_directory(directory);
    std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("WHITE ALBUM2",SDL_WINDOWPOS_UNDEFINED,SDL_WINDOWPOS_UNDEFINED,
        1280,720,SDL_WINDOW_RESIZABLE|SDL_WINDOW_ALLOW_HIGHDPI|(host.fullscreen?SDL_WINDOW_FULLSCREEN_DESKTOP:0)),SDL_DestroyWindow);
    if(!window)throw std::runtime_error(std::string("Create native game window: ")+SDL_GetError());
    auto canvas=SdlCanvas::create(window.get(),true);canvas->logical_size(1280,720);
    auto application=native_application(std::move(host.resources),directory,host.startup);
    SdlInput input(SDL_GetWindowID(window.get()));input.logical_size(1280,720);
    SdlApplication runner(*application,*canvas,host.driver,{},std::move(host.submitted));const auto epoch=std::chrono::steady_clock::now();
    runner.run(input,[&]{return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-epoch).count();},
        [](auto microseconds){std::this_thread::sleep_for(std::chrono::microseconds(microseconds));});
    return 0;
}
}
