#define SDL_MAIN_HANDLED
#include "../recompiled/special/runtime.hpp"
#include "../platform/window/sdl_host.hpp"
#include "../platform/window/native_widgets.hpp"
#include "../android/native/compiled_resources.hpp"
#include "../android/native/android_resources.hpp"
#include "../android/native/android_host.hpp"
#include "../platform/compat/diagnostic_chunks.hpp"
#include "../platform/render/particle_commands.hpp"
#include "../recompiled/native_glyphs.hpp"
#include "../recompiled/weather_threshold.hpp"
#include <SDL.h>
#include <SDL_main.h>
#include <android/log.h>
#include <string_view>
#include <vector>

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
        std::vector<std::string_view> arguments;for(int n=1;n<argc;++n)arguments.emplace_back(argv[n]);
        for(std::size_t n=0;n<arguments.size();++n){
            const auto argument=arguments[n];
            if(argument=="--limit-60-fps")options.limit_60_fps=true;
            else if(wa2::resource::language_launch_option(arguments,n,startup.language)){}
            else throw std::runtime_error("Unsupported Special launch argument: "+std::string(argument));
        }
        wa2::resource::validate_language_resources(startup.language,wa2::resource::ResourceTitle::special_contents);
        options.embedded_gl=true;options.mobile=true;options.widgets=wa2::window::native_widgets();
        options.frame_submitted=[]{wa2::android::frame_presented_with_text([]{
            const auto glyphs=wa2::aot::native_glyph_counts();
            const auto particles=wa2::graphics::particle_command_counts();
            const auto weather=wa2::aot::small_weather_counts;
            return wa2::android::GlyphTotals{glyphs.accepted,glyphs.tiles,0,0,0,0,
                particles.submitted,particles.tiles,particles.continued,
                weather.updates,weather.particles,weather.batched_updates};
        });};
        startup.window=wa2::window::sdl_host(std::move(options));
        startup.mount_resources=wa2::android::mount_compiled_resources;
        startup.io_diagnostic_sink=[](std::string_view label,std::string_view json){wa2::platform::compat::emit_counter_record(label,json,[](std::string_view part,std::string_view body){__android_log_print(ANDROID_LOG_INFO,"WA2IO","%.*s %.*s",int(part.size()),part.data(),int(body.size()),body.data());});};
        return int(wa2::special::run(std::move(startup)));
    }catch(const std::exception& error){__android_log_print(ANDROID_LOG_ERROR,"WA2Special","%s",error.what());wa2::android::report_game_failure(error.what());return 1;}
}
