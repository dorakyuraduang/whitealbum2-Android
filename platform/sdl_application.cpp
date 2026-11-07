#include "sdl_application.hpp"
#include "sdl_audio.hpp"
#include <SDL.h>

namespace wa2::platform {
std::shared_ptr<const image::Raster> save_thumbnail(const game::RenderFrame& frame){
    std::unique_ptr<SDL_Surface,decltype(&SDL_FreeSurface)> surface(SDL_CreateRGBSurfaceWithFormat(0,128,72,32,SDL_PIXELFORMAT_RGBA32),SDL_FreeSurface);
    if(!surface)throw std::runtime_error(std::string("Native save thumbnail surface: ")+SDL_GetError());
    auto* renderer=SDL_CreateSoftwareRenderer(surface.get());if(!renderer)throw std::runtime_error(std::string("Native save thumbnail renderer: ")+SDL_GetError());
    auto canvas=SdlCanvas::adopt(renderer);canvas->logical_size(1280,720);SdlPresentation presentation(*canvas);presentation.draw(frame);
    if(SDL_RenderFlush(renderer))throw std::runtime_error(std::string("Native save thumbnail flush: ")+SDL_GetError());
    if(SDL_LockSurface(surface.get()))throw std::runtime_error(std::string("Native save thumbnail memory: ")+SDL_GetError());
    struct Unlock {SDL_Surface* value;~Unlock(){SDL_UnlockSurface(value);}} unlock{surface.get()};
    auto pixels=std::make_shared<image::Raster>();pixels->width=128;pixels->height=72;pixels->rgba.resize(128u*72*4);
    for(unsigned y=0;y<72;++y)std::copy_n(static_cast<const std::uint8_t*>(surface->pixels)+std::size_t(y)*surface->pitch,128u*4,pixels->rgba.data()+y*128u*4);
    return pixels;
}
game::Input game_input(const InputSnapshot& events){
    game::Input input;
    input.advance=events.pressed_buttons[0] || events.pressed_keys[SDL_SCANCODE_SPACE] || events.pressed_keys[SDL_SCANCODE_RETURN];
    input.skip=events.keys[SDL_SCANCODE_LCTRL] || events.keys[SDL_SCANCODE_RCTRL];
    input.toggle_window=events.pressed_buttons[2];
    input.cancel=events.pressed_keys[SDL_SCANCODE_ESCAPE] || events.pressed_keys[SDL_SCANCODE_AC_BACK];
    input.selection.pointer=events.pointer;input.selection.x=events.x;input.selection.y=events.y;
    input.selection.activate=events.pressed_buttons[0];
    input.selection.confirm=events.pressed_keys[SDL_SCANCODE_SPACE] || events.pressed_keys[SDL_SCANCODE_RETURN];
    input.selection.navigation=int(events.pressed_keys[SDL_SCANCODE_DOWN])-int(events.pressed_keys[SDL_SCANCODE_UP]);
    input.pointer_held=events.buttons[0];
    input.page_delta=int(events.pressed_keys[SDL_SCANCODE_PAGEDOWN])-int(events.pressed_keys[SDL_SCANCODE_PAGEUP]);
    return input;
}
SdlApplication::SdlApplication(game::Application& application,SdlCanvas& canvas,game::DriverSettings settings,AudioOutputFactory factory,std::function<void()> submitted)
    :application_(application),driver_(application,settings),canvas_(canvas),presentation_(canvas),
    audio_(factory?std::move(factory):AudioOutputFactory([](audio::Mixer& mixer){return std::make_unique<SdlAudio>(mixer);})),submitted_(std::move(submitted)){
    if(!application.has_save_thumbnail())application.prepare_save_thumbnail([](game::Scene& scene){game::RenderFrame frame;frame.capture(scene);return save_thumbnail(frame);});
}
game::DriverFrame SdlApplication::pump(std::int64_t now,const InputSnapshot& events,const Clock& clock){
    const auto result=driver_.pump(now,{game_input(events),events.background || events.minimized || !events.focused,events.quit},clock);
    audio_.synchronize(application_.mixer(),application_.background());
    if(events.renderer_reset)presentation_.reset_textures();
    if(result.draw && !result.closed){frame_.capture(application_);presentation_.draw(frame_);canvas_.present(submitted_);}
    return result;
}
void SdlApplication::run(SdlInput& input,const Clock& clock,const Sleep& sleep){
    if(!clock || !sleep)throw std::invalid_argument("Missing native SDL clock or wait adapter");
    for(;;){input.begin_frame();input.poll();const auto result=pump(clock(),input.snapshot(),clock);if(result.closed)break;if(result.sleep_us)sleep(result.sleep_us);}
}
}
