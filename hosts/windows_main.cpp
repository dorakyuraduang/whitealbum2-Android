#define SDL_MAIN_HANDLED
#include "shared/startup.hpp"
#include "../platform/window/sdl_host.hpp"
#include "../platform/window/native_widgets.hpp"
#include <SDL.h>
#include <algorithm>
#include <iostream>
namespace {
std::filesystem::path resource_root(const std::filesystem::path& base){
    for(const auto& root:{base,base/"res",base.parent_path()/"res"}){
        std::error_code error;if(!std::filesystem::is_directory(root,error))continue;
        for(const auto& item:std::filesystem::directory_iterator(root,error)){auto extension=item.path().extension().string();for(auto& c:extension)if(c>='A' && c<='Z')c=char(c+32);if(extension==".pak" && item.is_regular_file(error))return root;}
        if(std::filesystem::is_directory(root/"IC",error))return root;
    }
    return base;
}
}
int main(int argc,char** argv){
    try{SDL_SetMainReady();char* value=SDL_GetBasePath();if(!value)throw std::runtime_error(SDL_GetError());std::filesystem::path base;
        try{base=std::filesystem::u8path(value);SDL_free(value);}catch(...){SDL_free(value);throw;}
        wa2::host::Startup startup;startup.options.data_root=resource_root(base);startup.options.allow_file_write=true;
        for(int n=1;n<argc;++n){const std::string argument=argv[n];if(argument=="--root" && n+1<argc)startup.options.data_root=std::filesystem::u8path(argv[++n]);
            else if(argument=="--report" && n+1<argc)startup.report_file=std::filesystem::u8path(argv[++n]);
            else if(argument=="--no-pause")continue;
            else if(argument=="--help"){std::cout<<"Place this program beside the original PAKs, or use --root <resource directory>.\n";return 0;}else throw std::runtime_error("Unknown launch argument: "+argument);}
        wa2::window::SdlHostOptions options;options.widgets=wa2::window::native_widgets();startup.window=wa2::window::sdl_host(std::move(options));
        return int(wa2::host::run(std::move(startup)));
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"WA2 startup failed",error.what(),nullptr);return 1;}
}
