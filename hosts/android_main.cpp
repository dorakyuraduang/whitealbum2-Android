#define SDL_MAIN_HANDLED
#include "shared/startup.hpp"
#include "../platform/window/sdl_host.hpp"
#include "../platform/window/native_widgets.hpp"
#include "../android/native/compiled_resources.hpp"
#include "../android/native/android_resources.hpp"
#include "../android/native/android_host.hpp"
#include "../platform/compat/diagnostic_chunks.hpp"
#include <SDL.h>
#include <SDL_main.h>
#include <android/log.h>
#include <string_view>
extern "C" int SDL_main(int argc,char** argv){
    try{SDL_SetMainReady();SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON,"1");
        SDL_SetHint(SDL_HINT_ORIENTATIONS,"LandscapeLeft LandscapeRight");
        char* value=SDL_GetPrefPath("WA2","shared-game");if(!value)throw std::runtime_error(SDL_GetError());wa2::host::Startup startup;
        try{startup.options.data_root=value;SDL_free(value);}catch(...){SDL_free(value);throw;}
        startup.options.allow_file_write=true;
        startup.files=wa2::android::compiled_save_files();
        std::vector<std::string_view> arguments;for(int n=1;n<argc;++n)arguments.emplace_back(argv[n]);
        for(std::size_t n=0;n<arguments.size();++n){const auto argument=arguments[n];
            if(argument=="--test-mode")startup.test_mode=true;
            else if(wa2::resource::language_launch_option(arguments,n,startup.language))continue;
            else throw std::runtime_error("Unknown Android launch argument: "+std::string(argument));}
        startup.report_file=startup.options.data_root/"wa2-runtime.json";
        startup.io_diagnostic_sink=[](std::string_view label,std::string_view json){wa2::platform::compat::emit_counter_record(label,json,[](std::string_view part,std::string_view body){__android_log_print(ANDROID_LOG_INFO,"WA2IO","%.*s %.*s",int(part.size()),part.data(),int(body.size()),body.data());});};
        wa2::window::SdlHostOptions options;options.embedded_gl=true;options.mobile=true;options.widgets=wa2::window::native_widgets();options.frame_submitted=wa2::android::frame_presented;
        startup.window=wa2::window::sdl_host(std::move(options));startup.mount_resources=wa2::android::mount_compiled_resources;
        return int(wa2::host::run(std::move(startup)));
    }catch(const std::exception& error){__android_log_print(ANDROID_LOG_ERROR,"WA2","%s",error.what());wa2::android::report_game_failure(error.what());return 1;}
}
