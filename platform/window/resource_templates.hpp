#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>
namespace wr {struct Image;}
namespace wa2::window {
using Name=std::variant<std::monostate,std::uint16_t,std::u16string>;
struct Font {std::uint16_t points{},weight{400};std::uint8_t italic{},charset{1};std::u16string face;};
struct Control {std::uint32_t id{},help{},style{},extended_style{};std::array<std::int16_t,4> bounds;Name type,title;std::vector<std::uint8_t> creation_data;};
struct DialogTemplate {bool extended{};std::uint32_t help{},style{},extended_style{};std::array<std::int16_t,4> bounds;Name menu,type;std::u16string title;std::optional<Font> font;std::vector<Control> controls;};
struct MenuItem {std::uint32_t id{},type{},state{},help{};std::u16string text;std::vector<MenuItem> children;bool operator==(const MenuItem&)const=default;};
struct MenuTemplate {bool extended{};std::uint32_t help{};std::vector<MenuItem> items;bool operator==(const MenuTemplate&)const=default;};
struct IconEntry {std::uint16_t width{},height{},planes{},bits{},id{};std::uint32_t bytes{};};
struct IconResource {std::uint16_t width{},height{};std::vector<std::uint8_t> dib;bool operator==(const IconResource&)const=default;};
DialogTemplate parse_dialog(std::span<const std::uint8_t>);
MenuTemplate parse_menu(std::span<const std::uint8_t>);
IconEntry select_icon(std::span<const std::uint8_t>,unsigned desired=32);
IconResource parse_icon_bitmap(std::span<const std::uint8_t>,unsigned width,unsigned height);
IconResource load_icon_resource(const wr::Image&,const Name&);
}
