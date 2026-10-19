#pragma once
#include "host.hpp"
struct SDL_Window;
namespace wa2::window {
// Native dialog/menu controls only. Source callbacks never run on the native
// UI thread: widget changes and commands return as owned, typed events.
class Widgets {
public:
    virtual ~Widgets()=default;
    virtual void parent(SDL_Window*,std::uint32_t source_window)=0;
    virtual void create(const DialogState&)=0;
    virtual void update(const DialogState&)=0;
    virtual void destroy(std::uint32_t)=0;
    virtual void show(std::uint32_t,bool)=0;
    virtual void title(std::uint32_t,std::u16string_view)=0;
    virtual void focus(std::uint32_t)=0;
    virtual void menu(std::uint32_t,const MenuTemplate&)=0;
    virtual std::optional<std::int32_t> system_metric(std::int32_t)const{return std::nullopt;}
    virtual std::vector<Event> events()=0;
};
}
