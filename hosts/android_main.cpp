#define SDL_MAIN_HANDLED
#include "shared/startup.hpp"
#include "../platform/window/sdl_host.hpp"
#include "../platform/window/native_widgets.hpp"
#include "../android/native/compiled_resources.hpp"
#include "../android/native/android_resources.hpp"
#include "../android/native/android_host.hpp"
#include "../android/native/subtitles_android.hpp"
#include "../platform/compat/diagnostic_chunks.hpp"
#include "../recompiled/native_glyphs.hpp"
#include "../recompiled/text_layout_replay_control.hpp"
#include "../recompiled/glyph_command_replay_control.hpp"
#include "../recompiled/text_run_replay_control.hpp"
#include "../recompiled/text_page_replay_control.hpp"
#include "../platform/render/text_buffers.hpp"
#include <SDL.h>
#include <SDL_main.h>
#include <android/log.h>
#include <string_view>
namespace {
std::atomic<std::uint64_t> measured_layout_hits{}, measured_glyph_hits{}, measured_run_hits{};
std::atomic<std::uint64_t> measured_page_spans{}, measured_page_glyphs{};
using Mode=wa2::android::TextProbeMode;
using Counter=wa2::android::TextProbeCounter;
void select_text_path(Mode mode){
    const bool run=mode==Mode::run || mode==Mode::both || (mode==Mode::shipped && wa2::aot::default_text_run_replay);
    wa2::aot::text_run_replay_enabled.store(run,std::memory_order_relaxed);
    wa2::graphics::enable_surface_glyph_commands(mode!=Mode::reference && mode!=Mode::run);
    const bool dense=mode==Mode::dense || mode==Mode::novel || mode==Mode::shipped;
    const bool bindings=mode==Mode::bindings || mode==Mode::novel || mode==Mode::shipped;
    wa2::graphics::enable_glyph_rollover_bind_reuse(bindings);
    wa2::aot::text_page_dense_enabled.store(dense,std::memory_order_relaxed);
    wa2::aot::text_page_replay_enabled.store(mode==Mode::page || mode==Mode::dense || mode==Mode::bindings || mode==Mode::novel ||
                                            (mode==Mode::shipped && wa2::aot::default_text_page_replay),
                                            std::memory_order_relaxed);
}
std::uint64_t read_text_counter(Counter counter){
    switch(counter){
        case Counter::layout_hits:return measured_layout_hits.load(std::memory_order_relaxed);
        case Counter::command_hits:return measured_glyph_hits.load(std::memory_order_relaxed);
        case Counter::run_hits:return measured_run_hits.load(std::memory_order_relaxed);
        case Counter::surface_commands:return wa2::graphics::surface_glyph_command_counts().submitted;
        case Counter::surface_continued:return wa2::graphics::surface_glyph_command_counts().continued;
        case Counter::surface_tiles:return wa2::graphics::surface_glyph_command_counts().continued_tiles;
        case Counter::rollovers:return wa2::graphics::glyph_pipeline_rollovers();
        case Counter::page_spans:return measured_page_spans.load(std::memory_order_relaxed);
        case Counter::page_glyphs:return measured_page_glyphs.load(std::memory_order_relaxed);
        case Counter::prepared_geometry:return wa2::graphics::surface_glyph_command_counts().prepared_geometry_hits;
        case Counter::retained_draws:return wa2::graphics::surface_glyph_command_counts().retained_geometry_draws;
        case Counter::binding_calls_avoided:return wa2::graphics::glyph_rollover_bindings_avoided();
        default:return 0;
    }
}
}
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
        startup.captions=wa2::android::selected_subtitles();
        measured_layout_hits.store(0, std::memory_order_relaxed);
        measured_glyph_hits.store(0, std::memory_order_relaxed);
        measured_run_hits.store(0, std::memory_order_relaxed);
        measured_page_spans.store(0, std::memory_order_relaxed);
        measured_page_glyphs.store(0, std::memory_order_relaxed);
        select_text_path(Mode::shipped);
        wa2::android::set_performance_switch(select_text_path,read_text_counter);
        struct ReleasePerformanceSwitch {~ReleasePerformanceSwitch(){wa2::android::set_performance_switch(nullptr);}} release_performance_switch;
        startup.io_diagnostic_sink=[](std::string_view label,std::string_view json){wa2::platform::compat::emit_counter_record(label,json,[](std::string_view part,std::string_view body){__android_log_print(ANDROID_LOG_INFO,"WA2IO","%.*s %.*s",int(part.size()),part.data(),int(body.size()),body.data());});};
        wa2::window::SdlHostOptions options;options.embedded_gl=true;options.mobile=true;options.widgets=wa2::window::native_widgets();
        options.frame_submitted=[]{
            wa2::android::frame_presented_with_text([]{
                const auto counts=wa2::aot::native_glyph_counts();
                measured_layout_hits.store(wa2::aot::text_layout_replay_hits, std::memory_order_relaxed);
                measured_glyph_hits.store(wa2::aot::glyph_command_replay_hits, std::memory_order_relaxed);
                measured_run_hits.store(wa2::aot::text_run_replay_hits, std::memory_order_relaxed);
                measured_page_spans.store(wa2::aot::text_page_replay_spans,std::memory_order_relaxed);
                measured_page_glyphs.store(wa2::aot::text_page_replay_glyphs,std::memory_order_relaxed);
                return wa2::android::GlyphTotals{counts.accepted,counts.tiles,wa2::aot::text_page_replay_glyphs,
                    wa2::graphics::surface_glyph_command_counts().prepared_geometry_hits,
                    wa2::graphics::glyph_pipeline_rollovers(),wa2::graphics::glyph_rollover_bindings_avoided()};
            });
        };
        startup.window=wa2::window::sdl_host(std::move(options));startup.mount_resources=wa2::android::mount_compiled_resources;
        return int(wa2::host::run(std::move(startup)));
    }catch(const std::exception& error){__android_log_print(ANDROID_LOG_ERROR,"WA2","%s",error.what());wa2::android::report_game_failure(error.what());return 1;}
}
