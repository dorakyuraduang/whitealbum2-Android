#include "android_host.hpp"
#include "android_resources.hpp"
#include "native_host.hpp"
#include <SDL.h>
#include <SDL_system.h>
#include <android/log.h>
#include <filesystem>

namespace wa2::android {
// Incomplete native migration entry, compiled but never launched by the
// verifier. The retained Android release continues to select the AOT host.
int run_game(int argc,char** argv){
    try{
        SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON,"1");SDL_SetHint(SDL_HINT_ANDROID_BLOCK_ON_PAUSE,"0");
        if(argc<0 || argc>33 || (argc && !argv))throw std::invalid_argument("Invalid native argument list");
        std::vector<std::string_view> arguments;for(int n=1;n<argc;++n){if(!argv[n])throw std::invalid_argument("Missing native argument");arguments.emplace_back(argv[n]);}
        auto startup=platform::native_startup(arguments);startup.seed=std::uint32_t(SDL_GetPerformanceCounter());
        platform::NativeHost host;host.resources=[](auto name){return try_open_resource_bank(name);};host.startup=startup;host.fullscreen=true;host.submitted=frame_presented;
        host.private_directory=[]{const auto* internal=SDL_AndroidGetInternalStoragePath();if(!internal || !*internal)throw std::runtime_error("Native private storage is unavailable");return std::filesystem::path(internal)/"native-game";};
        return platform::run_native_host(std::move(host));
    }catch(const std::exception& error){
        __android_log_print(ANDROID_LOG_ERROR,"WA2","%s",error.what());report_game_failure(error.what());return 1;
    }catch(...){
        __android_log_write(ANDROID_LOG_ERROR,"WA2","Unknown native game failure");report_game_failure("Unknown native game failure");return 1;
    }
}
}
