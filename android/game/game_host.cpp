#include "android_host.hpp"
#include "android_resources.hpp"
#include "native_application.hpp"
#include "sdl_application.hpp"
#include <SDL.h>
#include <SDL_system.h>
#include <android/log.h>
#include <chrono>
#include <filesystem>
#include <thread>

namespace wa2::android {
namespace {
using Clock=std::chrono::steady_clock;
struct SdlLifetime {
    SdlLifetime(){if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER|SDL_INIT_EVENTS))throw std::runtime_error(SDL_GetError());}
    ~SdlLifetime(){SDL_Quit();}
};
void require(bool ok,const char* name){if(!ok)throw std::runtime_error(std::string(name)+": "+SDL_GetError());}
}
// Incomplete native migration entry, compiled but never launched by the
// verifier. The retained Android release continues to select the AOT host.
int run_game(int argc,char** argv){
    try{
        SDL_SetHint(SDL_HINT_RENDER_DRIVER,"opengles2");SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,"linear");
        SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON,"1");SDL_SetHint(SDL_HINT_ANDROID_BLOCK_ON_PAUSE,"0");
        SdlLifetime sdl;
        std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("WHITE ALBUM2",SDL_WINDOWPOS_UNDEFINED,SDL_WINDOWPOS_UNDEFINED,
            1280,720,SDL_WINDOW_FULLSCREEN_DESKTOP|SDL_WINDOW_RESIZABLE|SDL_WINDOW_ALLOW_HIGHDPI),SDL_DestroyWindow);
        require(bool(window),"create game window");auto canvas=platform::SdlCanvas::create(window.get(),true);canvas->logical_size(1280,720);
        if(argc<0 || argc>33 || (argc && !argv))throw std::invalid_argument("Invalid native argument list");
        std::vector<std::string_view> arguments;for(int n=1;n<argc;++n){if(!argv[n])throw std::invalid_argument("Missing native argument");arguments.emplace_back(argv[n]);}
        auto startup=platform::native_startup(arguments);startup.seed=std::uint32_t(SDL_GetPerformanceCounter());
        const auto* internal=SDL_AndroidGetInternalStoragePath();if(!internal || !*internal)throw std::runtime_error("Native private storage is unavailable");
        const auto directory=std::filesystem::path(internal)/"native-game";std::filesystem::create_directory(directory);
        auto application=platform::native_application([](auto name){return try_open_resource_bank(name);},directory,startup);
        platform::SdlInput input(SDL_GetWindowID(window.get()));input.logical_size(1280,720);
        platform::SdlApplication runner(*application,*canvas,{}, {},frame_presented);const auto epoch=Clock::now();
        runner.run(input,[&]{return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now()-epoch).count();},
            [](auto microseconds){std::this_thread::sleep_for(std::chrono::microseconds(microseconds));});
        return 0;
    }catch(const std::exception& error){
        __android_log_print(ANDROID_LOG_ERROR,"WA2","%s",error.what());report_game_failure(error.what());return 1;
    }catch(...){
        __android_log_write(ANDROID_LOG_ERROR,"WA2","Unknown native game failure");report_game_failure("Unknown native game failure");return 1;
    }
}
}
