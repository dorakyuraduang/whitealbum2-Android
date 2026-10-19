#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <SDL.h>
#include <SDL_syswm.h>
#include "shell.hpp"
#include "codepage.hpp"
#include <array>
#include <algorithm>
#include <stdexcept>
namespace wa2::platform::compat {namespace {
std::wstring native(std::u16string_view value){return {value.begin(),value.end()};}
HWND owner_window(){auto* window=SDL_GetKeyboardFocus();if(!window)return nullptr;SDL_SysWMinfo info{};SDL_VERSION(&info.version);if(SDL_GetWindowWMInfo(window,&info) && info.subsystem==SDL_SYSWM_WINDOWS)return info.info.win.window;return nullptr;}
struct Apartment {HRESULT result{CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)};~Apartment(){if(SUCCEEDED(result))CoUninitialize();}};
std::optional<std::filesystem::path> pick(const FileRequest& request){
    Apartment apartment;const auto title=native(request.title),initial=request.initial.wstring();
    if(request.directory){BROWSEINFOW browse{};browse.hwndOwner=owner_window();browse.lpszTitle=title.c_str();browse.ulFlags=BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE;
        std::array<wchar_t,MAX_PATH> display{};browse.pszDisplayName=display.data();browse.lpfn=[](HWND hwnd,UINT message,LPARAM,LPARAM user)->int{if(message==BFFM_INITIALIZED && user)SendMessageW(hwnd,BFFM_SETSELECTIONW,TRUE,user);return 0;};browse.lParam=reinterpret_cast<LPARAM>(initial.c_str());
        const auto selected=SHBrowseForFolderW(&browse);if(!selected)return {};std::array<wchar_t,MAX_PATH> path{};const auto valid=SHGetPathFromIDListW(selected,path.data());CoTaskMemFree(selected);if(!valid)throw std::runtime_error("selected folder has no filesystem path");return std::filesystem::path(path.data());}
    std::vector<wchar_t> filters;for(const auto& filter:request.filters){for(const auto c:filter.label)filters.push_back(wchar_t(c));filters.push_back(0);for(const auto c:filter.pattern)filters.push_back(wchar_t(c));filters.push_back(0);}filters.push_back(0);
    std::array<wchar_t,32768> filename{};const auto suggested=native(request.suggested_filename);if(suggested.size()>=filename.size())throw std::runtime_error("initial filename limit");std::copy(suggested.begin(),suggested.end(),filename.begin());
    const auto extension=native(request.extension);OPENFILENAMEW dialog{};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=owner_window();dialog.lpstrFilter=filters.size()>1?filters.data():nullptr;dialog.nFilterIndex=1;dialog.lpstrFile=filename.data();dialog.nMaxFile=DWORD(filename.size());dialog.lpstrInitialDir=initial.c_str();dialog.lpstrTitle=title.empty()?nullptr:title.c_str();dialog.lpstrDefExt=extension.empty()?nullptr:extension.c_str();
    dialog.Flags=(request.flags&~DWORD(OFN_ENABLEHOOK|OFN_ENABLETEMPLATE|OFN_ENABLETEMPLATEHANDLE|OFN_ALLOWMULTISELECT))|OFN_NOCHANGEDIR;
    if(!(request.save?GetSaveFileNameW(&dialog):GetOpenFileNameW(&dialog))){const auto error=CommDlgExtendedError();if(error)throw std::runtime_error("native file dialog failed: "+std::to_string(error));return {};}
    return std::filesystem::path(filename.data());
}
}
ShellServices native_shell_services(const std::filesystem::path&){return {pick,[](std::u16string_view target,std::u16string_view verb){if(verb!=u"open" && verb!=u"explore")return false;auto encoded=encode_text(65001,target).bytes;std::string url(encoded.begin(),encoded.end());if(url.find("://")==std::string::npos && !url.starts_with("mailto:")){std::replace(url.begin(),url.end(),'\\','/');url="file:///"+url;}return SDL_OpenURL(url.c_str())==0;}};}
}
