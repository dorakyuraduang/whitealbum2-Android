#define SDL_MAIN_HANDLED
#include "../recompiled/special/runtime.hpp"
#include "../platform/window/sdl_host.hpp"
#include "../platform/window/native_widgets.hpp"
#include <windows.h>
#include <shellapi.h>

int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int){
    try{
        int count{};auto** arguments=CommandLineToArgvW(GetCommandLineW(),&count);
        if(!arguments)throw std::runtime_error("Cannot read Special launch arguments");
        wa2::host::Startup startup;bool root{};
        try{for(int n=1;n<count;++n){
            const std::wstring_view argument=arguments[n];
            if(argument==L"--root" && n+1<count){startup.options.data_root=arguments[++n];root=true;}
            else throw std::runtime_error("Expected --root pointing to external Special Contents resources");
        }}catch(...){LocalFree(arguments);throw;}
        LocalFree(arguments);
        if(!root)throw std::runtime_error("Supply --root with your Special Contents resource directory");
        startup.options.data_root=std::filesystem::weakly_canonical(startup.options.data_root);
        startup.options.allow_file_write=true;
        startup.report_file=startup.options.data_root/"wa2-special-runtime.json";
        wa2::window::SdlHostOptions options;options.widgets=wa2::window::native_widgets();
        startup.window=wa2::window::sdl_host(std::move(options));
        return int(wa2::special::run(std::move(startup)));
    }catch(const std::exception& error){MessageBoxA(nullptr,error.what(),"WA2 Special Contents",MB_OK|MB_ICONERROR);return 1;}
}
