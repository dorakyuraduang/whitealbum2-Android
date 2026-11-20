#define SDL_MAIN_HANDLED
#include "../recompiled/special/runtime.hpp"
#include "../platform/window/sdl_host.hpp"
#include "../platform/window/native_widgets.hpp"
#include "../android/native/compiled_resources.hpp"
#include "../android/native/android_resources.hpp"
#include "../android/native/android_host.hpp"
#include <SDL.h>
#include <SDL_main.h>
#include <android/log.h>
#include <string_view>

extern "C" int SDL_main(int argc,char** argv){
    try{
        SDL_SetMainReady();SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON,"1");
        SDL_SetHint(SDL_HINT_ORIENTATIONS,"LandscapeLeft LandscapeRight");
        wa2::host::Startup startup;
        char* value=SDL_GetPrefPath("WA2","special-contents");if(!value)throw std::runtime_error(SDL_GetError());
        try{startup.options.data_root=std::filesystem::path(value)/"special-contents";SDL_free(value);}catch(...){SDL_free(value);throw;}
        std::filesystem::create_directories(startup.options.data_root);
        startup.options.allow_file_write=true;startup.files=wa2::android::compiled_save_files();
        startup.report_file=startup.options.data_root/"wa2-runtime.json";
        wa2::window::SdlHostOptions options;
        for(int n=1;n<argc;++n){
            const std::string_view argument=argv[n];
            if(argument=="--limit-60-fps")options.limit_60_fps=true;
            else throw std::runtime_error("Unsupported Special launch argument: "+std::string(argument));
        }
        options.embedded_gl=true;options.mobile=true;options.widgets=wa2::window::native_widgets();
        options.frame_submitted=wa2::android::frame_presented;
        startup.window=wa2::window::sdl_host(std::move(options));
        startup.mount_resources=wa2::android::mount_compiled_resources;
        startup.io_diagnostic_sink=[](std::string_view label,std::string_view json){__android_log_print(ANDROID_LOG_INFO,"WA2Special","%.*s %.*s",int(label.size()),label.data(),int(json.size()),json.data());};
        return int(wa2::special::run(std::move(startup)));
    }catch(const std::exception& error){__android_log_print(ANDROID_LOG_ERROR,"WA2Special","%s",error.what());wa2::android::report_game_failure(error.what());return 1;}
}
