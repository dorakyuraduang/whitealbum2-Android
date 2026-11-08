#include "native_host.hpp"
#include "native_desktop.hpp"
#if defined(WA2_NATIVE_ENTRY_AUDIT)
#define SDLMAIN_DECLSPEC __declspec(dllexport)
#endif
#include <SDL.h>

// SDL's own Windows main shim supplies UTF-8 arguments. This wrapper contains
// paths and terminal error reporting; the application/loop is the Android one.
int main(int argc,char** argv){
    try{
        if(argc<0 || argc>33 || (argc && !argv))throw std::invalid_argument("Invalid native desktop argument list");std::vector<std::string_view> arguments;
        for(int n=1;n<argc;++n){if(!argv[n])throw std::invalid_argument("Missing native desktop argument");arguments.emplace_back(argv[n]);}
        auto startup=wa2::platform::native_desktop_startup(arguments);
        SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON,"101");SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON_SMALL,"101");
        std::unique_ptr<char,decltype(&SDL_free)> base(SDL_GetBasePath(),SDL_free);if(!base)throw std::runtime_error("Native executable directory is unavailable");
        const auto directory=wa2::platform::native_desktop_directory(wa2::platform::native_utf8_path(base.get()),std::filesystem::current_path(),startup.resources);
        wa2::platform::NativeHost host;host.resources=wa2::platform::native_desktop_banks(directory);host.legacy_settings=wa2::platform::native_desktop_legacy_settings(directory);host.legacy_saves=wa2::platform::native_desktop_legacy_saves(directory);host.startup=startup.game;host.startup.seed=std::uint32_t(SDL_GetPerformanceCounter());
        host.storage=[directory]{return wa2::platform::game_directory(directory);};
        return wa2::platform::run_native_host(std::move(host));
    }catch(const std::exception& error){SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"WA2 native startup failed",error.what(),nullptr);return 1;}
}
