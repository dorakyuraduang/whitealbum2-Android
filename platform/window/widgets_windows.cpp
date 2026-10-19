#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <SDL.h>
#include <SDL_syswm.h>
#include "native_widgets.hpp"
#include "menu_update.hpp"
#include <algorithm>
#include <cstring>
#include <exception>
#include <map>
#include <stdexcept>
namespace wa2::window {namespace {
std::wstring wide(std::u16string_view value){return std::wstring(value.begin(),value.end());}
struct TemplateBytes {
    std::vector<std::uint8_t> bytes;
    void word(std::uint16_t v){bytes.push_back(std::uint8_t(v));bytes.push_back(std::uint8_t(v>>8));}
    void dword(std::uint32_t v){word(std::uint16_t(v));word(std::uint16_t(v>>16));}
    void string(std::u16string_view value){for(auto c:value)word(c);word(0);}
    void name(const Name& value){if(const auto* ordinal=std::get_if<std::uint16_t>(&value)){word(0xffff);word(*ordinal);}else if(const auto* text=std::get_if<std::u16string>(&value))string(*text);else word(0);}
    void align(){while(bytes.size()%4)bytes.push_back(0);}
};
std::vector<std::uint8_t> dialog_bytes(const DialogState& source){
    const auto& d=source.definition;TemplateBytes out;out.word(1);out.word(0xffff);out.dword(d.help);out.dword(d.extended_style);
    out.dword((d.style&~std::uint32_t(WS_VISIBLE))|(d.font?DS_SETFONT:0));out.word(std::uint16_t(source.controls.size()));for(auto v:d.bounds)out.word(std::uint16_t(v));
    out.name(d.menu);out.name(d.type);out.string(d.title);
    if(d.font){out.word(d.font->points);out.word(d.font->weight);out.bytes.push_back(d.font->italic);out.bytes.push_back(d.font->charset);out.string(d.font->face);}
    for(const auto& c:source.controls){out.align();out.dword(c.definition.help);out.dword(c.definition.extended_style);out.dword(c.definition.style);
        for(auto v:c.definition.bounds)out.word(std::uint16_t(v));out.dword(c.definition.id);out.name(c.definition.type);out.string(c.text);
        if(c.definition.creation_data.size()>65533)throw std::runtime_error("dialog creation-data limit");
        out.word(std::uint16_t(c.definition.creation_data.size()));out.bytes.insert(out.bytes.end(),c.definition.creation_data.begin(),c.definition.creation_data.end());}
    return std::move(out.bytes);
}
class WindowsWidgets final:public Widgets {
    struct Dialog {WindowsWidgets* owner{};DialogState state;HWND window{};bool updating{};};
    HWND main{};std::uint32_t main_id{};std::map<std::uint32_t,std::unique_ptr<Dialog>> dialogs;
    std::map<std::uint32_t,HMENU> menus;std::vector<Event> pending;
    std::map<std::uint32_t,MenuTemplate> menu_models;
    std::exception_ptr failure;
    static constexpr wchar_t Property[]=L"WA2.CommonWidgets";
    WNDPROC original{};
    static LRESULT CALLBACK main_procedure(HWND hwnd,UINT message,WPARAM wp,LPARAM lp)noexcept{
        auto* self=reinterpret_cast<WindowsWidgets*>(GetPropW(hwnd,Property));if(!self)return DefWindowProcW(hwnd,message,wp,lp);
        if(message==WM_COMMAND && !lp){try{self->pending.push_back({Event::Kind::message,self->main_id,WM_COMMAND,std::uint32_t(wp),0});}catch(...){self->failure=std::current_exception();}return 0;}
        return CallWindowProcW(self->original,hwnd,message,wp,lp);
    }
    static INT_PTR CALLBACK dialog_procedure(HWND hwnd,UINT message,WPARAM wp,LPARAM lp)noexcept{
        auto* d=reinterpret_cast<Dialog*>(GetWindowLongPtrW(hwnd,DWLP_USER));
        if(message==WM_INITDIALOG){d=reinterpret_cast<Dialog*>(lp);d->window=hwnd;SetWindowLongPtrW(hwnd,DWLP_USER,lp);return TRUE;}
        if(!d || d->updating)return FALSE;
        try{return d->owner->event(*d,message,wp,lp);}catch(...){d->owner->failure=std::current_exception();return FALSE;}
    }
    INT_PTR event(Dialog& d,UINT message,WPARAM wp,LPARAM lp){
        if(message==WM_CLOSE){pending.push_back({Event::Kind::message,d.state.token,WM_CLOSE,0,0});return TRUE;}
        if(message==WM_COMMAND){const auto found=std::find_if(d.state.controls.begin(),d.state.controls.end(),[&](const auto& c){return c.definition.id==LOWORD(wp);});
            if(found==d.state.controls.end())return FALSE;const auto notification=HIWORD(wp);const auto hwnd=reinterpret_cast<HWND>(lp);
            if(hwnd && notification==EN_CHANGE && std::get_if<std::uint16_t>(&found->definition.type) && std::get<std::uint16_t>(found->definition.type)==129){
                const int size=GetWindowTextLengthW(hwnd);if(size>1048576)throw std::runtime_error("edit text limit");std::wstring value(std::size_t(size)+1,L'\0');const auto count=GetWindowTextW(hwnd,value.data(),int(value.size()));
                Event event{Event::Kind::text,d.state.token};event.control=found->definition.id;event.text.assign(value.begin(),value.begin()+count);pending.push_back(std::move(event));
            }else if(hwnd && notification==LBN_SELCHANGE && std::get_if<std::uint16_t>(&found->definition.type) && std::get<std::uint16_t>(found->definition.type)==131){
                Event event{Event::Kind::selection,d.state.token};event.control=found->definition.id;event.value=int(SendMessageW(hwnd,LB_GETCURSEL,0,0));pending.push_back(std::move(event));
            }else if(hwnd && notification==BN_CLICKED){Event event{Event::Kind::check,d.state.token};event.control=found->definition.id;event.value=int(SendMessageW(hwnd,BM_GETCHECK,0,0));pending.push_back(std::move(event));}
            pending.push_back({Event::Kind::message,d.state.token,WM_COMMAND,std::uint32_t(wp),found->token});return TRUE;
        }
        if(message==WM_HSCROLL || message==WM_VSCROLL){const auto hwnd=reinterpret_cast<HWND>(lp);const auto id=GetDlgCtrlID(hwnd);const auto found=std::find_if(d.state.controls.begin(),d.state.controls.end(),[&](const auto& c){return c.definition.id==std::uint32_t(id);});if(found==d.state.controls.end())return FALSE;
            Event event{Event::Kind::position,d.state.token};event.control=found->definition.id;event.value=int(SendMessageW(hwnd,TBM_GETPOS,0,0));pending.push_back(std::move(event));
            pending.push_back({Event::Kind::message,d.state.token,message,std::uint32_t(wp),found->token});return TRUE;}
        return FALSE;
    }
    Dialog& get(std::uint32_t id){const auto found=dialogs.find(id);if(found==dialogs.end())throw std::runtime_error("unknown native dialog");return *found->second;}
    HMENU make_menu(const std::vector<MenuItem>& items,bool popup){auto menu=popup?CreatePopupMenu():CreateMenu();if(!menu)throw std::runtime_error("cannot create native menu");
        try{for(const auto& item:items){MENUITEMINFOW info{sizeof(info)};info.fMask=MIIM_ID|MIIM_FTYPE|MIIM_STATE;info.wID=item.id;info.fType=item.type;info.fState=item.state;
                auto text=wide(item.text);if(!(item.type&MFT_SEPARATOR)){info.fMask|=MIIM_STRING;info.dwTypeData=text.data();}
                if(!item.children.empty()){info.fMask|=MIIM_SUBMENU;info.hSubMenu=make_menu(item.children,true);}
                if(!InsertMenuItemW(menu,GetMenuItemCount(menu),TRUE,&info)){if(info.hSubMenu)DestroyMenu(info.hSubMenu);throw std::runtime_error("cannot append native menu");}}return menu;
        }catch(...){DestroyMenu(menu);throw;}}
    void update_menu(HMENU menu,const std::vector<MenuItem>& before,const std::vector<MenuItem>& after){
        for(unsigned n=0;n<after.size();++n){const auto& old=before[n];const auto& value=after[n];if(old==value)continue;
            const bool layout=old.id!=value.id || old.type!=value.type || old.text!=value.text;
            if(layout || old.state!=value.state){MENUITEMINFOW info{sizeof(info)};info.fMask=MIIM_STATE;info.fState=value.state;std::wstring text;
                if(layout){info.fMask|=MIIM_ID|MIIM_FTYPE;info.wID=value.id;info.fType=value.type;
                    if(!(value.type&MFT_SEPARATOR)){text=wide(value.text);info.fMask|=MIIM_STRING;info.dwTypeData=text.data();}}
                if(!SetMenuItemInfoW(menu,n,TRUE,&info))throw std::runtime_error("cannot update native menu item");
            }
            if(!value.children.empty() && old.children!=value.children){const auto child=GetSubMenu(menu,int(n));
                if(!child)throw std::runtime_error("native submenu missing");update_menu(child,old.children,value.children);}
        }
    }
public:
    std::optional<std::int32_t> system_metric(std::int32_t index)const override{return GetSystemMetrics(index);}
    ~WindowsWidgets()override{for(auto& [id,d]:dialogs)if(d->window)DestroyWindow(d->window);if(main && IsWindow(main)){if(original && reinterpret_cast<WNDPROC>(GetWindowLongPtrW(main,GWLP_WNDPROC))==main_procedure)SetWindowLongPtrW(main,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(original));RemovePropW(main,Property);SetMenu(main,nullptr);}for(auto [id,menu]:menus)DestroyMenu(menu);}
    void parent(SDL_Window* window,std::uint32_t id)override{SDL_SysWMinfo info{};SDL_VERSION(&info.version);if(!SDL_GetWindowWMInfo(window,&info) || info.subsystem!=SDL_SYSWM_WINDOWS)throw std::runtime_error("SDL native window unavailable");main=info.info.win.window;main_id=id;
        if(!SetPropW(main,Property,this))throw std::runtime_error("cannot attach native widgets");SetLastError(0);original=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(main,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(main_procedure)));if(!original)throw std::runtime_error("cannot attach native menu procedure");}
    void create(const DialogState& source)override{INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_WIN95_CLASSES};if(!InitCommonControlsEx(&controls))throw std::runtime_error("cannot initialize native controls");
        const auto bytes=dialog_bytes(source);auto value=std::make_unique<Dialog>();value->owner=this;value->state=source;auto* d=value.get();dialogs.emplace(source.token,std::move(value));
        const auto hwnd=CreateDialogIndirectParamW(GetModuleHandleW(nullptr),reinterpret_cast<const DLGTEMPLATE*>(bytes.data()),main,dialog_procedure,reinterpret_cast<LPARAM>(d));
        if(!hwnd){dialogs.erase(source.token);throw std::runtime_error("cannot create native dialog");}d->window=hwnd;update(source);if(source.visible)ShowWindow(hwnd,SW_SHOW);}
    void update(const DialogState& source)override{auto& d=get(source.token);d.updating=true;
        try{for(const auto& c:source.controls){const auto hwnd=GetDlgItem(d.window,int(c.definition.id));if(!hwnd)throw std::runtime_error("native dialog control missing");
                EnableWindow(hwnd,c.enabled);ShowWindow(hwnd,c.visible?SW_SHOW:SW_HIDE);const auto text=wide(c.text);if(GetWindowTextLengthW(hwnd)!=int(text.size()))SetWindowTextW(hwnd,text.c_str());else{std::wstring current(text.size()+1,L'\0');GetWindowTextW(hwnd,current.data(),int(current.size()));if(std::wstring_view(current.data())!=text)SetWindowTextW(hwnd,text.c_str());}
                if(const auto* type=std::get_if<std::uint16_t>(&c.definition.type)){
                    if(*type==128)SendMessageW(hwnd,BM_SETCHECK,WPARAM(c.check),0);
                    else if(*type==131){SendMessageW(hwnd,LB_RESETCONTENT,0,0);for(const auto& item:c.items){const auto text=wide(item);SendMessageW(hwnd,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));}SendMessageW(hwnd,LB_SETCURSEL,WPARAM(c.selection),0);}
                }else if(const auto* type=std::get_if<std::u16string>(&c.definition.type);type && *type==u"msctls_trackbar32"){
                    SendMessageW(hwnd,TBM_SETRANGEMIN,0,c.minimum);SendMessageW(hwnd,TBM_SETRANGEMAX,0,c.maximum);SendMessageW(hwnd,TBM_SETPOS,TRUE,c.position);
                    SendMessageW(hwnd,TBM_SETLINESIZE,0,c.line_size);SendMessageW(hwnd,TBM_SETPAGESIZE,0,c.page_size);SendMessageW(hwnd,TBM_CLEARTICS,FALSE,0);for(const auto tick:c.ticks)SendMessageW(hwnd,TBM_SETTIC,0,tick);
                }}d.state=source;d.updating=false;
        }catch(...){d.updating=false;throw;}}
    void destroy(std::uint32_t id)override{auto& d=get(id);DestroyWindow(d.window);dialogs.erase(id);}
    void show(std::uint32_t id,bool value)override{ShowWindow(get(id).window,value?SW_SHOW:SW_HIDE);}
    void title(std::uint32_t id,std::u16string_view text)override{SetWindowTextW(get(id).window,wide(text).c_str());}
    void focus(std::uint32_t id)override{if(dialogs.contains(id)){SetForegroundWindow(get(id).window);return;}for(auto& [dialog_id,d]:dialogs)for(const auto& c:d->state.controls)if(c.token==id){SetFocus(GetDlgItem(d->window,int(c.definition.id)));return;}throw std::runtime_error("unknown native focus target");}
    void menu(std::uint32_t id,const MenuTemplate& source)override{if(id!=main_id)throw std::runtime_error("unknown native menu owner");
        const auto previous=menu_models.find(id);auto change=previous==menu_models.end()?MenuUpdate::structure:menu_update(previous->second.items,source.items);
        if(change==MenuUpdate::none)return;
        const bool layout=change==MenuUpdate::layout || change==MenuUpdate::structure;RECT previous_client{};if(layout)GetClientRect(main,&previous_client);
        if(change==MenuUpdate::structure){const auto native=source.items.empty()?nullptr:make_menu(source.items,false);
            if(!SetMenu(main,native)){if(native)DestroyMenu(native);throw std::runtime_error("cannot set native menu");}
            if(const auto old=menus.find(id);old!=menus.end()){DestroyMenu(old->second);menus.erase(old);}if(native)menus.emplace(id,native);
        }else{const auto native=menus.find(id);if(native==menus.end())throw std::runtime_error("native menu missing");update_menu(native->second,previous->second.items,source.items);}
        menu_models[id]=source;DrawMenuBar(main);
        RECT current{},outer{};if(layout && GetClientRect(main,&current) && GetWindowRect(main,&outer) && (current.right!=previous_client.right || current.bottom!=previous_client.bottom))
            SetWindowPos(main,nullptr,0,0,outer.right-outer.left+previous_client.right-current.right,outer.bottom-outer.top+previous_client.bottom-current.bottom,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    }
    std::vector<Event> events()override{for(auto& [id,d]:dialogs){MSG message;while(PeekMessageW(&message,d->window,0,0,PM_REMOVE))if(!IsDialogMessageW(d->window,&message)){TranslateMessage(&message);DispatchMessageW(&message);}}if(failure)std::rethrow_exception(failure);auto result=std::move(pending);pending.clear();return result;}
};
}
std::shared_ptr<Widgets> native_widgets(){return std::make_shared<WindowsWidgets>();}
}
