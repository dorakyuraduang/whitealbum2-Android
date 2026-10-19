#define SDL_MAIN_HANDLED
#include "sdl_host.hpp"
#include "hidden_popups.hpp"
#include "pointer_coordinates.hpp"
#include "../compat/codepage.hpp"
#include <SDL.h>
#include <algorithm>
#include <bitset>
#include <cmath>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <thread>
namespace wa2::window {namespace {
std::string utf8(std::u16string_view text){const auto encoded=platform::compat::encode_text(65001,text).bytes;return std::string(encoded.begin(),encoded.end());}
unsigned virtual_key(SDL_Keycode key){
    if(key>=SDLK_a && key<=SDLK_z)return unsigned(key-SDLK_a+'A');if(key>=SDLK_0 && key<=SDLK_9)return unsigned(key);
    if(key>=SDLK_F1 && key<=SDLK_F12)return unsigned(key-SDLK_F1+0x70);
    if(key>=SDLK_F13 && key<=SDLK_F24)return unsigned(key-SDLK_F13+0x7c);
    switch(key){case SDLK_BACKSPACE:return 8;case SDLK_TAB:return 9;case SDLK_RETURN:case SDLK_KP_ENTER:return 13;
    case SDLK_LSHIFT:case SDLK_RSHIFT:return 0x10;case SDLK_LCTRL:case SDLK_RCTRL:return 0x11;case SDLK_LALT:case SDLK_RALT:return 0x12;
    case SDLK_PAUSE:return 0x13;case SDLK_CAPSLOCK:return 0x14;case SDLK_ESCAPE:case SDLK_AC_BACK:return 0x1b;
    case SDLK_SPACE:return 0x20;case SDLK_PAGEUP:return 0x21;case SDLK_PAGEDOWN:return 0x22;case SDLK_END:return 0x23;case SDLK_HOME:return 0x24;
    case SDLK_LEFT:return 0x25;case SDLK_UP:return 0x26;case SDLK_RIGHT:return 0x27;case SDLK_DOWN:return 0x28;
    case SDLK_PRINTSCREEN:return 0x2c;case SDLK_INSERT:return 0x2d;case SDLK_DELETE:return 0x2e;
    case SDLK_KP_0:return 0x60;case SDLK_KP_1:return 0x61;case SDLK_KP_2:return 0x62;case SDLK_KP_3:return 0x63;case SDLK_KP_4:return 0x64;
    case SDLK_KP_5:return 0x65;case SDLK_KP_6:return 0x66;case SDLK_KP_7:return 0x67;case SDLK_KP_8:return 0x68;case SDLK_KP_9:return 0x69;
    case SDLK_KP_MULTIPLY:return 0x6a;case SDLK_KP_PLUS:return 0x6b;case SDLK_KP_MINUS:return 0x6d;case SDLK_KP_PERIOD:return 0x6e;case SDLK_KP_DIVIDE:return 0x6f;
    case SDLK_NUMLOCKCLEAR:return 0x90;case SDLK_SCROLLLOCK:return 0x91;case SDLK_SEMICOLON:return 0xba;case SDLK_EQUALS:return 0xbb;
    case SDLK_COMMA:return 0xbc;case SDLK_MINUS:return 0xbd;case SDLK_PERIOD:return 0xbe;case SDLK_SLASH:return 0xbf;case SDLK_BACKQUOTE:return 0xc0;
    case SDLK_LEFTBRACKET:return 0xdb;case SDLK_BACKSLASH:return 0xdc;case SDLK_RIGHTBRACKET:return 0xdd;case SDLK_QUOTE:return 0xde;default:return 0;}
}
class SdlHost final:public Host {
    SdlHostOptions options;SDL_Window* window{};std::uint32_t id{},sdl_id{};
    std::vector<SDL_Window*> retired_windows;
    WindowSpec spec;std::map<std::uint32_t,DialogState> dialogs;
    std::array<std::uint32_t,2> render_extent{};
    HiddenPopups popups;
    const std::thread::id owner{std::this_thread::get_id()};bool initialized{},has_focus{true};int cursor_count{};
    std::array<std::uint16_t,256> keys{};std::array<int,2> mouse{};std::array<bool,5> buttons{};
    std::optional<std::pair<SDL_TouchID,SDL_FingerID>> finger;
    struct InputSample {unsigned button{};bool down{};std::array<double,2> raw{};std::array<std::int32_t,2> client{};PointerCoordinates geometry;};
    mutable InputSample last_input;std::array<InputSample,16> click_samples{};
    std::uint64_t click_count{},left_queries{},left_down_queries{},left_nonzero_queries{};
    void enter()const{if(std::this_thread::get_id()!=owner)throw std::runtime_error("SDL host used from a foreign thread");}
    void initialize(){if(initialized)return;SDL_SetMainReady();
        // Text events remain available for physical keyboards. Native edit
        // controls own the IME; the game canvas must not create SDL DummyEdit.
        SDL_SetHintWithPriority(SDL_HINT_ENABLE_SCREEN_KEYBOARD,"0",SDL_HINT_OVERRIDE);
        if(SDL_InitSubSystem(SDL_INIT_VIDEO))throw std::runtime_error(SDL_GetError());initialized=true;
        if(SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,options.embedded_gl?SDL_GL_CONTEXT_PROFILE_ES:SDL_GL_CONTEXT_PROFILE_COMPATIBILITY) ||
           SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,options.embedded_gl?3:2) || SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,options.embedded_gl?0:1) ||
           SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1) || SDL_GL_SetAttribute(SDL_GL_RED_SIZE,8) || SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE,8) || SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE,8))throw std::runtime_error(SDL_GetError());
    }
    bool accept(std::uint32_t target)const{return !target || target==sdl_id;}
    std::array<std::uint32_t,2> source_extent()const{return {std::uint32_t(std::max(0,spec.bounds.width)),std::uint32_t(std::max(0,spec.bounds.height))};}
    PointerCoordinates pointer_coordinates()const{PointerCoordinates value;if(!window)return value;SDL_GetWindowSize(window,&value.window_width,&value.window_height);SDL_GL_GetDrawableSize(window,&value.drawable_width,&value.drawable_height);
        return PointerCoordinates::from_client(value.window_width,value.window_height,value.drawable_width,value.drawable_height,
            client_viewport(id,value.drawable_width,value.drawable_height));}
    void coordinates(double x,double y){const auto geometry=pointer_coordinates();if(const auto point=geometry.source(x,y)){mouse=*point;last_input={0,false,{x,y},*point,geometry};}}
    std::uint32_t mouse_state()const{return (buttons[0]?1:0)|(buttons[2]?2:0)|(buttons[1]?16:0)|(buttons[3]?32:0)|(buttons[4]?64:0)|(keys[0x10]&0x8000?4:0)|(keys[0x11]&0x8000?8:0);}
    std::uint32_t mouse_param()const{return packed_pointer(mouse);}
    void message(std::vector<Event>& output,std::uint32_t number,std::uint32_t wp=0,std::uint32_t lp=0){Event event{Event::Kind::message,id,number,wp,lp};event.client_cursor=mouse;output.push_back(std::move(event));}
    void button(std::vector<Event>& out,unsigned button,bool down){if(button>=5)return;const auto old=buttons[button];if(old==down)return;buttons[button]=down;
        auto sample=last_input;sample.button=button+1;sample.down=down;click_samples[click_count++%click_samples.size()]=sample;
        const auto vk=button==0?1:button==1?4:button==2?2:button==3?5:6;keys[vk]=down?0x8001:std::uint16_t(keys[vk]&1);
        const unsigned number=button==0?(down?0x201:0x202):button==1?(down?0x207:0x208):button==2?(down?0x204:0x205):(down?0x20b:0x20c);
        message(out,number,mouse_state()|(button>=3?(button-2)<<16:0),mouse_param());
    }
    void release(std::vector<Event>& out){for(unsigned b=0;b<5;++b)button(out,b,false);for(auto& key:keys)key&=1;finger.reset();}
    void consume(const SDL_Event& event,std::vector<Event>& output){
        switch(event.type){
        case SDL_QUIT:message(output,0x10);break;
        case SDL_APP_WILLENTERBACKGROUND:has_focus=false;release(output);message(output,0x1c,0);message(output,6,0);break;
        case SDL_APP_DIDENTERFOREGROUND:has_focus=true;message(output,0x1c,1);message(output,6,1);break;
        case SDL_WINDOWEVENT:if(!accept(event.window.windowID))break;switch(event.window.event){
            case SDL_WINDOWEVENT_CLOSE:message(output,0x10);break;
            case SDL_WINDOWEVENT_FOCUS_GAINED:has_focus=true;message(output,0x1c,1);message(output,6,1);message(output,7);break;
            case SDL_WINDOWEVENT_FOCUS_LOST:has_focus=false;release(output);message(output,0x1c,0);message(output,6);message(output,8);break;
            case SDL_WINDOWEVENT_EXPOSED:message(output,0xf);break;
            case SDL_WINDOWEVENT_MINIMIZED:release(output);message(output,5,1);break;
            case SDL_WINDOWEVENT_RESTORED:{const auto extent=source_extent();message(output,5,0,(extent[0]&65535)|(extent[1]<<16));break;}
            default:break;}break;
        case SDL_KEYDOWN:case SDL_KEYUP:if(accept(event.key.windowID)){const auto vk=virtual_key(event.key.keysym.sym);if(!vk)break;const bool down=event.type==SDL_KEYDOWN,previous=(keys[vk]&0x8000)!=0;
            keys[vk]=down?0x8001:std::uint16_t(keys[vk]&1);const auto modifiers=event.key.keysym.mod;
            for(auto [target,bits]:{std::pair{0x10,KMOD_SHIFT},std::pair{0x11,KMOD_CTRL},std::pair{0x12,KMOD_ALT}})if(unsigned(target)!=vk)keys[target]=std::uint16_t((keys[target]&1)|((modifiers&bits)?0x8000:0));
            const auto lp=1u|(unsigned(event.key.keysym.scancode&255)<<16)|((modifiers&KMOD_ALT)?1u<<29:0)|(previous?1u<<30:0)|(!down?1u<<31:0);
            message(output,(modifiers&KMOD_ALT)?(down?0x104:0x105):(down?0x100:0x101),vk,lp);
            if(down && (vk==8 || vk==9 || vk==13))message(output,0x102,vk,lp);
        }break;
        case SDL_TEXTINPUT:if(accept(event.text.windowID)){const std::string value(event.text.text);const auto wide=platform::compat::decode_text(65001,std::span(reinterpret_cast<const std::uint8_t*>(value.data()),value.size()));
            const auto encoded=platform::compat::encode_text(936,wide).bytes;for(auto byte:encoded)message(output,0x102,byte,1);}break;
        case SDL_MOUSEMOTION:if(accept(event.motion.windowID) && event.motion.which!=SDL_TOUCH_MOUSEID && !finger){coordinates(float(event.motion.x),float(event.motion.y));message(output,0x200,mouse_state(),mouse_param());}break;
        case SDL_MOUSEBUTTONDOWN:case SDL_MOUSEBUTTONUP:if(accept(event.button.windowID) && event.button.which!=SDL_TOUCH_MOUSEID){if(!finger)coordinates(float(event.button.x),float(event.button.y));button(output,unsigned(event.button.button)-1,event.type==SDL_MOUSEBUTTONDOWN);}break;
        case SDL_MOUSEWHEEL:if(accept(event.wheel.windowID) && event.wheel.which!=SDL_TOUCH_MOUSEID){if(!finger)coordinates(event.wheel.mouseX,event.wheel.mouseY);const auto amount=std::clamp(event.wheel.y,-100,100)*120*(event.wheel.direction==SDL_MOUSEWHEEL_FLIPPED?-1:1);message(output,0x20a,mouse_state()|(std::uint32_t(amount)<<16),mouse_param());}break;
        case SDL_FINGERDOWN:case SDL_FINGERMOTION:case SDL_FINGERUP:if(accept(event.tfinger.windowID)){const auto value=std::pair{event.tfinger.touchId,event.tfinger.fingerId};if(event.type==SDL_FINGERDOWN && !finger)finger=value;if(finger!=value)break;
            int w=0,h=0;SDL_GetWindowSize(window,&w,&h);coordinates(event.tfinger.x*w,event.tfinger.y*h);message(output,0x200,mouse_state(),mouse_param());button(output,0,event.type!=SDL_FINGERUP);if(event.type==SDL_FINGERUP)finger.reset();}break;
        default:break;
        }
    }
public:
    explicit SdlHost(SdlHostOptions value):options(std::move(value)){if(!options.widgets)throw std::invalid_argument("native widgets adapter required");}
    ~SdlHost()override{for(const auto& [id,dialog]:dialogs)try{options.widgets->destroy(id);}catch(...){}if(window)SDL_DestroyWindow(window);for(auto* retired:retired_windows)SDL_DestroyWindow(retired);if(initialized)SDL_QuitSubSystem(SDL_INIT_VIDEO);}
    bool embedded_gl()const noexcept override{return options.embedded_gl;}
    SDL_Window* acquire(std::uint32_t source,std::uint32_t width,std::uint32_t height,bool windowed)override{enter();if(!window || source!=id)throw std::runtime_error("unknown source render window");
        const auto extent=backbuffer_size(source,width,height,windowed);if(!extent)throw std::runtime_error("invalid source backbuffer dimensions");render_extent=*extent;
        if(!options.mobile && !windowed && SDL_SetWindowFullscreen(window,SDL_WINDOW_FULLSCREEN_DESKTOP))throw std::runtime_error(SDL_GetError());return window;}
    void frame_submitted()override{if(options.frame_submitted)options.frame_submitted();}
    void create(const WindowSpec& value)override{enter();if(window){popups.create(value,id);return;}if(value.parent)throw std::runtime_error("main SDL window cannot have a source owner");initialize();spec=value;id=value.token;
        auto flags=std::uint32_t(SDL_WINDOW_OPENGL|SDL_WINDOW_ALLOW_HIGHDPI);if(options.mobile)flags|=SDL_WINDOW_FULLSCREEN_DESKTOP;else flags|=SDL_WINDOW_RESIZABLE;
        if(!(value.style&0x10000000))flags|=SDL_WINDOW_HIDDEN;
        window=SDL_CreateWindow(utf8(value.title).c_str(),value.bounds.x<0?SDL_WINDOWPOS_CENTERED:value.bounds.x,value.bounds.y<0?SDL_WINDOWPOS_CENTERED:value.bounds.y,value.bounds.width,value.bounds.height,flags);
        if(!window)throw std::runtime_error(SDL_GetError());sdl_id=SDL_GetWindowID(window);options.widgets->parent(window,id);SDL_StartTextInput();}
    void destroy(std::uint32_t target)override{enter();if(const auto found=dialogs.find(target);found!=dialogs.end()){options.widgets->destroy(target);dialogs.erase(found);return;}
        if(popups.contains(target)){popups.destroy(target);return;}
        if(target!=id || !window)throw std::runtime_error("unknown SDL window");SDL_StopTextInput();SDL_HideWindow(window);retired_windows.push_back(window);popups.clear();window=nullptr;render_extent={};id=sdl_id=0;}
    void show(std::uint32_t target,bool visible)override{enter();if(dialogs.contains(target)){dialogs.at(target).visible=visible;options.widgets->show(target,visible);return;}if(popups.contains(target)){popups.show(target,visible);return;}if(target!=id || !window)throw std::runtime_error("unknown SDL window");if(visible)SDL_ShowWindow(window);else SDL_HideWindow(window);}
    void title(std::uint32_t target,std::u16string_view title)override{enter();if(dialogs.contains(target)){options.widgets->title(target,title);return;}if(popups.contains(target)){popups.get(target).title=title;return;}if(target!=id || !window)throw std::runtime_error("unknown SDL window");spec.title=title;SDL_SetWindowTitle(window,utf8(title).c_str());}
    void position(std::uint32_t target,const Bounds& bounds,std::uint32_t flags)override{enter();if(popups.contains(target)){popups.position(target,bounds,flags);return;}if(target!=id || !window)throw std::runtime_error("positioning an unknown main window");spec.bounds=bounds;if(options.mobile)return;if(!(flags&2))SDL_SetWindowPosition(window,bounds.x,bounds.y);if(!(flags&1))SDL_SetWindowSize(window,bounds.width,bounds.height);if(flags&0x40)SDL_ShowWindow(window);if(flags&0x80)SDL_HideWindow(window);}
    Bounds client(std::uint32_t target)const override{enter();if(dialogs.contains(target)){const auto& bounds=dialogs.at(target).definition.bounds;return {0,0,bounds[2]*2,bounds[3]*2};}if(popups.contains(target)){const auto& b=popups.get(target).bounds;return {0,0,b.width,b.height};}if(target!=id || !window)throw std::runtime_error("unknown SDL client");const auto extent=source_extent();return {0,0,int(extent[0]),int(extent[1])};}
    Bounds desktop()const override{enter();if(!initialized)const_cast<SdlHost*>(this)->initialize();SDL_DisplayMode mode{};if(SDL_GetCurrentDisplayMode(0,&mode))throw std::runtime_error(SDL_GetError());return {0,0,mode.w,mode.h};}
    std::optional<std::int32_t> system_metric(std::int32_t index)const override{enter();if(!initialized)const_cast<SdlHost*>(this)->initialize();if(const auto native=options.widgets->system_metric(index))return native;return Host::system_metric(index);}
    bool visible(std::uint32_t target)const override{enter();if(dialogs.contains(target))return dialogs.at(target).visible;if(popups.contains(target))return false;return target==id && window && (SDL_GetWindowFlags(window)&SDL_WINDOW_SHOWN);}
    bool focused(std::uint32_t target)const override{enter();return target==id && window && has_focus;}
    void focus(std::uint32_t target)override{enter();if(target==id && window){SDL_RaiseWindow(window);return;}options.widgets->focus(target);}
    std::array<std::int32_t,2> cursor()const override{enter();if(window && !finger && SDL_GetMouseFocus()==window){int x=0,y=0;SDL_GetMouseState(&x,&y);const auto geometry=pointer_coordinates();if(const auto point=geometry.source(x,y)){last_input={0,false,{double(x),double(y)},*point,geometry};return *point;}}return mouse;}
    void cursor(std::int32_t x,std::int32_t y)override{enter();mouse={x,y};if(const auto point=pointer_coordinates().window(x,y))SDL_WarpMouseInWindow(window,(*point)[0],(*point)[1]);}
    int show_cursor(bool show)override{enter();cursor_count+=show?1:-1;SDL_ShowCursor(cursor_count>=0?SDL_ENABLE:SDL_DISABLE);return cursor_count;}
    std::uint16_t key(std::uint32_t vk)override{enter();if(vk>=keys.size())return 0;const auto value=keys[vk];keys[vk]&=0x8000;if(vk==1){++left_queries;left_down_queries+=bool(value&0x8000);left_nonzero_queries+=bool(value);}return value;}
    std::string input_report()const override{
        std::ostringstream out;const auto array=[&](const auto& values){out<<'[';bool first=true;for(const auto value:values){if(!first)out<<',';first=false;out<<value;}out<<']';};
        const auto sample=[&](const InputSample& s){const auto& g=s.geometry;out<<"{\"button\":"<<s.button<<",\"down\":"<<(s.down?"true":"false")<<",\"window_xy\":";array(s.raw);out<<",\"client_xy\":";array(s.client);
            out<<",\"window_units\":["<<g.window_width<<','<<g.window_height<<"],\"drawable_pixels\":["<<g.drawable_width<<','<<g.drawable_height<<"],\"source_client_pixels\":["<<g.source_width<<','<<g.source_height<<"],\"drawable_content\":";array(g.content);out<<",\"source_client_content\":";array(g.client_rectangle());out<<'}';};
        out<<"{\"schema\":\"wa2.pointer-mapping.v3\",\"backbuffer_pixels\":["<<render_extent[0]<<','<<render_extent[1]<<"],\"source_client_size\":["<<spec.bounds.width<<','<<spec.bounds.height<<"],\"button_events\":"<<click_count<<",\"left_key_queries\":"<<left_queries<<",\"left_key_down_queries\":"<<left_down_queries<<",\"left_key_nonzero_queries\":"<<left_nonzero_queries<<",\"last\":";sample(last_input);out<<",\"recent_buttons\":[";
        const auto count=std::min<std::uint64_t>(click_count,click_samples.size());for(std::uint64_t n=0;n<count;++n){if(n)out<<',';sample(click_samples[(click_count-count+n)%click_samples.size()]);}
        out<<"],\"own_window_events_only\":true,\"screen_access\":false}";return out.str();
    }
    std::vector<Event> events(unsigned timeout)override{enter();std::vector<Event> result;if(initialized){SDL_Event event;if(timeout && SDL_WaitEventTimeout(&event,int(std::min(timeout,50u))))consume(event,result);while(SDL_PollEvent(&event))consume(event,result);}auto widgets=options.widgets->events();result.insert(result.end(),std::make_move_iterator(widgets.begin()),std::make_move_iterator(widgets.end()));return result;}
    std::uint32_t message_box(std::u16string_view text,std::u16string_view title,std::uint32_t flags)override{enter();initialize();std::vector<SDL_MessageBoxButtonData> buttons;
        const auto add=[&](int id,const char* label){buttons.push_back({id==1 || id==6?SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT:0u,id,label});};
        switch(flags&15){case 0:add(1,"OK");break;case 1:add(1,"OK");add(2,"Cancel");break;case 2:add(3,"Abort");add(4,"Retry");add(5,"Ignore");break;case 3:add(6,"Yes");add(7,"No");add(2,"Cancel");break;case 4:add(6,"Yes");add(7,"No");break;case 5:add(4,"Retry");add(2,"Cancel");break;default:throw std::runtime_error("unsupported message box button set");}
        const auto caption=utf8(title),message=utf8(text);SDL_MessageBoxData data{(flags&0x10)?SDL_MESSAGEBOX_ERROR:SDL_MESSAGEBOX_INFORMATION,window,caption.c_str(),message.c_str(),int(buttons.size()),buttons.data(),nullptr};int selected{};
        if(SDL_ShowMessageBox(&data,&selected))throw std::runtime_error(SDL_GetError());return selected<0?2u:std::uint32_t(selected);}
    void create_dialog(const DialogState& value)override{enter();dialogs.emplace(value.token,value);options.widgets->create(value);}
    void update_dialog(const DialogState& value)override{enter();dialogs.at(value.token)=value;options.widgets->update(value);}
    void menu(std::uint32_t target,const MenuTemplate& value)override{enter();if(popups.contains(target))return;options.widgets->menu(target,value);}
};
}
std::shared_ptr<Host> sdl_host(SdlHostOptions options){return std::make_shared<SdlHost>(std::move(options));}
}
