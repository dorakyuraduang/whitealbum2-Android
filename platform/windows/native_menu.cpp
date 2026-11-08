#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "native_menu.hpp"
#include <SDL.h>
#include <SDL_syswm.h>
#include <windows.h>
#include <algorithm>
#include <exception>
#include <limits>
#include <span>

namespace wa2::platform {
namespace {
constexpr wchar_t class_name[]=L"WA2NativeCommandMenu";
constexpr unsigned first_command=100;
using Command=game::UserCommand;
struct Group {const wchar_t* title;unsigned count;std::array<Command,10> commands;};
constexpr std::array<Group,4> groups{{
    {L"画面设置",4,{Command::fullscreen,Command::window_hd,Command::window_medium,Command::window_small}},
    {L"环境设置",3,{Command::settings,Command::movie_high,Command::movie_low}},
    {L"系统",10,{Command::save,Command::load,Command::quick_save,Command::quick_load,Command::backlog,Command::hide,Command::automatic,Command::skip,Command::title,Command::quit}},
    {L"帮助",2,{Command::about,Command::environment}}
}};
static_assert([]{std::uint32_t seen{};for(const auto& group:groups){if(group.count>group.commands.size())return false;for(unsigned n=0;n<group.count;++n){const auto bit=game::command_bit(group.commands[n]);if(!bit || (seen&bit))return false;seen|=bit;}}return seen==(1u<<unsigned(Command::count))-1;}());
std::runtime_error failure(const char* operation){return std::runtime_error(std::string(operation)+": Windows error "+std::to_string(GetLastError()));}
std::wstring wide(std::string_view text){
    if(text.empty() || text.size()>256)throw std::invalid_argument("Invalid native menu label");
    const auto count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),nullptr,0);if(!count)throw failure("Decode native menu label");
    std::wstring result(unsigned(count),L'\0');if(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),result.data(),count)!=count)throw failure("Decode native menu label");return result;
}
class WindowsMenu final:public NativeMenuAdapter {
    HWND game_{},window_{};HINSTANCE instance_{};HMENU root_{};
    std::array<HMENU,unsigned(game::UserCommand::count)> parents_{};
    std::function<void()> suspend_;NativeMenuGap gap_;std::exception_ptr error_;
    std::uint64_t state_{std::numeric_limits<std::uint64_t>::max()};std::uint32_t window_id_{};
    bool registered_{},attached_{},tracking_{},closing_{};
    static LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM word,LPARAM parameter)noexcept{
        auto* self=reinterpret_cast<WindowsMenu*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if(message==WM_NCCREATE){self=static_cast<WindowsMenu*>(reinterpret_cast<CREATESTRUCTW*>(parameter)->lpCreateParams);self->window_=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self)return DefWindowProcW(window,message,word,parameter);
        try{return self->message(window,message,word,parameter);}catch(...){self->error_=std::current_exception();EndMenu();return 0;}
    }
    void hide(bool restore){
        if(!window_)return;const bool owned_focus=GetForegroundWindow()==window_;
        if(IsWindowVisible(window_))ShowWindow(window_,SW_HIDE);
        const auto foreground=GetForegroundWindow();if(restore && owned_focus && !closing_ && (!foreground || foreground==window_) && IsWindow(game_) && !IsIconic(game_))SetForegroundWindow(game_);
    }
    void choose(unsigned id){
        if(id<first_command || id>=first_command+parents_.size())throw std::invalid_argument("Unknown native desktop menu command");
        const auto command=game::UserCommand(id-first_command);hide(true);
        if(!(std::uint32_t(state_)&game::command_bit(command)))return;
        auto event=user_command_event(command);event.user.windowID=window_id_;
        if(SDL_PushEvent(&event)!=1)throw std::runtime_error(std::string("Queue native desktop menu command: ")+SDL_GetError());
    }
    void detach()noexcept{
        if(attached_){if(!SetMenu(window_,nullptr))root_=nullptr;attached_=false;}
    }
    LRESULT message(HWND window,UINT message,WPARAM word,LPARAM parameter){
        switch(message){
        case WM_ENTERMENULOOP:tracking_=true;gap_.enter(suspend_);return 0;
        case WM_EXITMENULOOP:tracking_=false;hide(true);return 0;
        case WM_COMMAND:if(!closing_ && HIWORD(word)==0)choose(LOWORD(word));return 0;
        case WM_CLOSE:if(!closing_)choose(first_command+unsigned(game::UserCommand::quit));return 0;
        case WM_KEYDOWN:if(word==VK_ESCAPE){hide(true);return 0;}break;
        case WM_ACTIVATE:if(LOWORD(word)==WA_INACTIVE && !tracking_)hide(false);break;
        case WM_DESTROY:detach();break;
        case WM_NCDESTROY:window_=nullptr;SetWindowLongPtrW(window,GWLP_USERDATA,0);break;
        }
        return DefWindowProcW(window,message,word,parameter);
    }
    void cleanup()noexcept{
        closing_=true;if(window_){detach();DestroyWindow(window_);}if(root_)DestroyMenu(root_);root_=nullptr;
        if(registered_)UnregisterClassW(class_name,instance_);registered_=false;
    }
    void group(const wchar_t* title,std::span<const game::UserCommand> commands){
        const auto menu=CreatePopupMenu();if(!menu)throw failure("Create native submenu");
        if(!AppendMenuW(root_,MF_POPUP,reinterpret_cast<UINT_PTR>(menu),title)){DestroyMenu(menu);throw failure("Attach native submenu");}
        for(auto command:commands){const auto index=unsigned(command);if(parents_.at(index))throw std::logic_error("Duplicate native desktop menu command");
            const auto label=wide(game::user_command_labels[index]);if(!AppendMenuW(menu,MF_STRING|MF_GRAYED,first_command+index,label.c_str()))throw failure("Create native menu command");parents_[index]=menu;}
    }
public:
    WindowsMenu(SDL_Window* owner,std::function<void()> suspend):suspend_(std::move(suspend)){
        if(!owner || !suspend_)throw std::invalid_argument("Missing native desktop menu owner or clock service");
        try{
            SDL_SysWMinfo info{};SDL_VERSION(&info.version);if(!SDL_GetWindowWMInfo(owner,&info) || info.subsystem!=SDL_SYSWM_WINDOWS)throw std::runtime_error("Native desktop menu requires the SDL Windows window backend");
            game_=info.info.win.window;window_id_=SDL_GetWindowID(owner);instance_=GetModuleHandleW(nullptr);
            WNDCLASSEXW type{};type.cbSize=sizeof(type);type.lpfnWndProc=procedure;type.hInstance=instance_;type.hCursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512));type.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_MENU+1);type.lpszClassName=class_name;
            registered_=RegisterClassExW(&type)!=0;if(!registered_){if(GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)throw failure("Register native desktop menu");WNDCLASSEXW existing{};existing.cbSize=sizeof(existing);if(!GetClassInfoExW(instance_,class_name,&existing) || existing.lpfnWndProc!=procedure)throw std::runtime_error("Native desktop menu class belongs to another service");}
            root_=CreateMenu();if(!root_)throw failure("Create native desktop menubar");
            for(const auto& item:groups)group(item.title,std::span(item.commands).first(item.count));
            for(auto menu:parents_)if(!menu)throw std::logic_error("Missing native desktop menu command");
            window_=CreateWindowExW(WS_EX_TOOLWINDOW,class_name,L"",WS_POPUP|WS_BORDER,0,0,1,1,game_,nullptr,instance_,this);if(!window_)throw failure("Create native desktop menubar window");
            if(!SetMenu(window_,root_))throw failure("Attach native desktop menubar");attached_=true;
        }catch(...){cleanup();throw;}
    }
    ~WindowsMenu()override{cleanup();}
    void publish(game::UserMenuState state)override{
        if(error_)std::rethrow_exception(error_);const auto packed=state.packed();if(state_==packed)return;
        constexpr auto mask=(1u<<unsigned(game::UserCommand::count))-1;if((state.enabled|state.checked)&~mask)throw std::invalid_argument("Unknown native desktop menu state bit");
        for(unsigned n=0;n<parents_.size();++n){const auto bit=1u<<n;
            if(EnableMenuItem(parents_[n],first_command+n,MF_BYCOMMAND|(state.enabled&bit?MF_ENABLED:MF_GRAYED))==UINT(-1))throw failure("Enable native menu command");
            if(CheckMenuItem(parents_[n],first_command+n,MF_BYCOMMAND|(state.checked&bit?MF_CHECKED:MF_UNCHECKED))==DWORD(-1))throw failure("Check native menu command");}
        state_=packed;if(window_ && IsWindowVisible(window_) && !DrawMenuBar(window_))throw failure("Update native desktop menubar");
    }
    void filter(InputSnapshot& input)override{
        if(error_)std::rethrow_exception(error_);gap_.filter(input);if(tracking_)return;
        const auto foreground=GetForegroundWindow();
        if(input.background || input.minimized || !IsWindowVisible(game_) || IsIconic(game_) || (foreground!=game_ && foreground!=window_)){hide(false);return;}
        RECT client{};POINT origin{},pointer{};if(!GetClientRect(game_,&client) || !ClientToScreen(game_,&origin) || !GetCursorPos(&pointer))throw failure("Locate native desktop menubar");
        const auto height=std::max(1,GetSystemMetrics(SM_CYMENUSIZE));
        if(pointer.x<origin.x || pointer.x>=origin.x+client.right || pointer.y<origin.y || pointer.y>=origin.y+height){hide(true);return;}
        const auto bar_height=height+2*GetSystemMetrics(SM_CYBORDER);RECT current{};
        if(!GetWindowRect(window_,&current) || current.left!=origin.x || current.top!=origin.y || current.right-current.left!=client.right || current.bottom-current.top!=bar_height || !IsWindowVisible(window_))
            if(!SetWindowPos(window_,HWND_TOP,origin.x,origin.y,client.right,bar_height,SWP_NOACTIVATE|SWP_SHOWWINDOW))throw failure("Position native desktop menubar");
    }
};
}
std::unique_ptr<NativeMenuAdapter> windows_native_menu(SDL_Window* window,std::function<void()> suspend){return std::make_unique<WindowsMenu>(window,std::move(suspend));}
}
