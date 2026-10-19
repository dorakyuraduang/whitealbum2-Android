#pragma once
#include "resource_templates.hpp"
#include "../render/surface_host.hpp"
#include "system_metrics.hpp"
#include <array>
#include <functional>
#include <optional>
#include <span>
namespace wa2::window {
struct Bounds {std::int32_t x{},y{},width{},height{};};
struct WindowSpec {std::uint32_t token{},parent{},style{},extended_style{};Bounds bounds;std::u16string title;};
struct ControlState {
    Control definition;
    std::uint32_t token{};
    std::u16string text;
    std::vector<std::u16string> items;
    std::int32_t selection{-1},position{},minimum{},maximum{100},check{},line_size{1},page_size{20};
    std::int32_t selection_start{},selection_end{};
    std::uint32_t font{};std::vector<std::int32_t> ticks;
    std::optional<IconResource> icon;
    bool enabled{true},visible{true};
};
struct DialogState {std::uint32_t token{},owner{};DialogTemplate definition;std::vector<ControlState> controls;bool visible{true};};
struct Event {
    enum class Kind {message,text,selection,position,check} kind{Kind::message};
    std::uint32_t window{},message{},wparam{},lparam{},control{};
    std::int32_t value{};std::u16string text;
    // Position at this event, in source client pixels. Batches must preserve
    // each sample rather than using the final cursor for every queued MSG.
    std::optional<std::array<std::int32_t,2>> client_cursor;
};
// Native widgets, SDL input and window sizing live behind this interface.
// Source window procedures and control semantics are implemented once in gui.cpp.
class Host:public graphics::SurfaceHost {
public:
    void source_cursor(std::int32_t x,std::int32_t y)final{cursor(x,y);}
    int cursor_visible(bool show)final{return show_cursor(show);}
    virtual void create(const WindowSpec&)=0;
    virtual void destroy(std::uint32_t)=0;
    virtual void show(std::uint32_t,bool)=0;
    virtual void title(std::uint32_t,std::u16string_view)=0;
    virtual void position(std::uint32_t,const Bounds&,std::uint32_t flags)=0;
    virtual Bounds client(std::uint32_t) const=0;
    std::array<int,2> client_size(std::uint32_t source_window)const final{const auto b=client(source_window);return {b.width,b.height};}
    virtual Bounds desktop() const=0;
    virtual std::optional<std::int32_t> system_metric(std::int32_t index)const{
        const auto b=desktop();return surface_metric(index,b.x,b.y,b.width,b.height);
    }
    virtual bool visible(std::uint32_t) const=0;
    virtual bool focused(std::uint32_t) const=0;
    virtual void focus(std::uint32_t)=0;
    virtual std::array<std::int32_t,2> cursor() const=0;
    virtual void cursor(std::int32_t,std::int32_t)=0;
    virtual int show_cursor(bool)=0;
    virtual std::uint16_t key(std::uint32_t)=0;
    virtual std::string input_report() const{return "{}";}
    // Only events belonging to this process. A positive timeout waits for one.
    virtual std::vector<Event> events(unsigned timeout_ms)=0;
    virtual std::uint32_t message_box(std::u16string_view text,std::u16string_view title,std::uint32_t flags)=0;
    virtual void create_dialog(const DialogState&)=0;
    virtual void update_dialog(const DialogState&)=0;
    virtual void menu(std::uint32_t,const MenuTemplate&)=0;
};
}
