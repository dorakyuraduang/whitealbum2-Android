#pragma once
#include <array>
#include <stdexcept>
#include <string_view>

namespace wa2::game {
struct MenuHeader {std::string_view name;unsigned y{},minimum_height{};};
inline MenuHeader menu_header(int theme,bool title){
    if(theme<0 || theme>6)throw std::invalid_argument("Native menu header theme is out of range");
    constexpr std::array<std::string_view,4> names{"sys_05010.tga","sys_05011.tga","sys_05012.tga","sys_04010.tga"};
    const bool full=title || theme>2;return {names[full?3u:unsigned(theme)],full?0u:16u,full?168u:184u};
}
}
