#include "android_host.hpp"
#include "android_resources.hpp"
#include "scene.hpp"
#include "sdl_audio.hpp"
#include "sdl_input.hpp"
#include "sdl_movie.hpp"
#include "sdl_scene.hpp"
#include <SDL.h>
#include <android/log.h>
#include <algorithm>
#include <chrono>
#include <memory>
#include <stdexcept>
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
int run_game(int,char**){
    try{
        SDL_SetHint(SDL_HINT_RENDER_DRIVER,"opengles2");
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,"linear");
        SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON,"1");
        SDL_SetHint(SDL_HINT_ANDROID_BLOCK_ON_PAUSE,"0");
        SdlLifetime sdl;
        std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("WHITE ALBUM2",SDL_WINDOWPOS_UNDEFINED,SDL_WINDOWPOS_UNDEFINED,
            1280,720,SDL_WINDOW_FULLSCREEN_DESKTOP|SDL_WINDOW_RESIZABLE|SDL_WINDOW_ALLOW_HIGHDPI),SDL_DestroyWindow);
        require(bool(window),"create game window");auto canvas=platform::SdlCanvas::create(window.get(),true);canvas->logical_size(1280,720);
        platform::SdlScene display(*canvas);platform::SdlMovie movie_display(*canvas);
        game::Scene scene([](auto name){return try_open_resource_bank(name);},std::uint32_t(SDL_GetPerformanceCounter()));
        platform::SdlAudio audio(*scene.mixer());if(!audio.open())throw std::runtime_error("open native audio: "+audio.status().error);
        platform::SdlInput input(SDL_GetWindowID(window.get()));input.logical_size(1280,720);
        // The first device build enters the original introductory script. It
        // executes actual dialogue, characters, sound and movies from Wa2Res.
        scene.start("1001");
        auto previous=Clock::now();auto next_present=previous;std::int64_t active_us{},accumulator=16667;
        bool paused=false,running=true;game::Input pending;std::int64_t held_since=-1;
        while(running){
            input.begin_frame();input.poll();const auto& events=input.snapshot();if(events.quit)break;
            const auto now=Clock::now();const auto delta=std::chrono::duration_cast<std::chrono::microseconds>(now-previous).count();previous=now;
            const bool suspend=events.background || events.minimized || !events.focused;
            if(suspend!=paused){paused=suspend;scene.pause(paused,active_us);audio.pause(paused);pending={};held_since=-1;accumulator=0;next_present=now;}
            if(paused){std::this_thread::sleep_for(std::chrono::milliseconds(16));continue;}
            active_us+=std::max<std::int64_t>(0,delta);accumulator+=std::clamp<std::int64_t>(delta,0,133336);
            if(events.renderer_reset){display.reset_textures();movie_display.reset();}
            if(events.pressed_buttons[0])held_since=active_us;
            if(!events.buttons[0])held_since=-1;
            pending.advance|=events.pressed_buttons[0] || events.pressed_keys[SDL_SCANCODE_SPACE] || events.pressed_keys[SDL_SCANCODE_RETURN];
            pending.toggle_window|=events.pressed_buttons[2] || events.pressed_keys[SDL_SCANCODE_AC_BACK];
            pending.skip=events.keys[SDL_SCANCODE_LCTRL] || events.keys[SDL_SCANCODE_RCTRL] || (held_since>=0 && active_us-held_since>=500000);
            // Keep the game's original 60 Hz logic duration while allowing
            // presentations at up to 120 Hz. A stall cannot build an unbounded
            // catch-up queue and the HUD counts only submitted game frames.
            unsigned updates=0;
            while(accumulator>=16667 && updates++<8){
                running=scene.update(active_us,pending)!=script::RunState::stopped;pending.advance=pending.toggle_window=false;accumulator-=16667;
                if(!running)break;
            }
            if(accumulator>=16667)accumulator%=16667;
            display.background(scene.background(),scene.environment());display.sprites(scene.foreground());
            if(auto frame=scene.movie())movie_display.draw(*frame);else if(!scene.movie_active())movie_display.reset();
            display.sprites(scene.dialogue());canvas->present(frame_presented);
            next_present=std::max(next_present+std::chrono::microseconds(8333),Clock::now());std::this_thread::sleep_until(next_present);
        }
        audio.close();scene.stop_movie();return 0;
    }catch(const std::exception& error){
        __android_log_print(ANDROID_LOG_ERROR,"WA2","%s",error.what());report_game_failure(error.what());return 1;
    }catch(...){
        __android_log_write(ANDROID_LOG_ERROR,"WA2","Unknown native game failure");report_game_failure("Unknown native game failure");return 1;
    }
}
}
