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
        wa2::host::Startup startup;bool root{};wa2::window::SdlHostOptions options;
        try{for(int n=1;n<count;++n){
            const std::wstring_view argument=arguments[n];
            if(argument==L"--root" && n+1<count){startup.options.data_root=arguments[++n];root=true;}
            else if(argument==L"--language" && n+1<count){const std::wstring_view tag=arguments[++n];startup.language.language=wa2::resource::parse_language(std::string(tag.begin(),tag.end()));}
            else if(argument==L"--limit-60-fps")options.limit_60_fps=true;
            else throw std::runtime_error("Expected --root pointing to external Special Contents resources");
        }}catch(...){LocalFree(arguments);throw;}
        LocalFree(arguments);
        if(!root)throw std::runtime_error("Supply --root with your Special Contents resource directory");
        wa2::resource::validate_language_resources(startup.language,wa2::resource::ResourceTitle::special_contents);
        startup.options.data_root=std::filesystem::weakly_canonical(startup.options.data_root);
        startup.options.allow_file_write=true;
        startup.report_file=startup.options.data_root/"wa2-special-runtime.json";
        options.widgets=wa2::window::native_widgets();
        startup.window=wa2::window::sdl_host(std::move(options));
        return int(wa2::special::run(std::move(startup)));
    }catch(const std::exception& error){MessageBoxA(nullptr,error.what(),"WA2 Special Contents",MB_OK|MB_ICONERROR);return 1;}
}
