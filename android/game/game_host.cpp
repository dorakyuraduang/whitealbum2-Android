#include "android_host.hpp"
#include "android_resources.hpp"
#include "native_host.hpp"
#include "user_menu_jni.hpp"
#include "resource_jni.hpp"
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
        // SDL's resizable-window policy otherwise selects FULL_USER and can
        // override the manifest's sensorLandscape request with portrait.
        SDL_SetHint(SDL_HINT_ORIENTATIONS,"LandscapeLeft LandscapeRight");
        if(argc<0 || argc>33 || (argc && !argv))throw std::invalid_argument("Invalid native argument list");
        std::vector<std::string_view> arguments;for(int n=1;n<argc;++n){if(!argv[n])throw std::invalid_argument("Missing native argument");arguments.emplace_back(argv[n]);}
        auto startup=platform::native_startup(arguments);startup.seed=std::uint32_t(SDL_GetPerformanceCounter());
        platform::NativeHost host;host.resources=[](auto name){return try_open_resource_bank(name);};host.legacy_settings=[](auto name){return try_open_legacy_settings(name);};host.startup=startup;host.fullscreen=true;host.submitted=frame_presented;host.menu_status=publish_user_menu;
        host.legacy_saves.open=[](auto name){return try_open_legacy_save(name);};host.legacy_saves.list=[]{std::vector<game::LegacySaveFile> files;for(auto& item:legacy_save_metadata())files.push_back({std::move(item.name),item.size,item.modified_millis,{}});return files;};
        host.storage=[]{return selected_game_directory();};
        return platform::run_native_host(std::move(host));
    }catch(const std::exception& error){
        __android_log_print(ANDROID_LOG_ERROR,"WA2","%s",error.what());report_game_failure(error.what());return 1;
    }catch(...){
        __android_log_write(ANDROID_LOG_ERROR,"WA2","Unknown native game failure");report_game_failure("Unknown native game failure");return 1;
    }
}
}
