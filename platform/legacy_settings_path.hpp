#pragma once
#include <string_view>

namespace wa2::platform {
inline bool legacy_settings_path(std::string_view name)noexcept{
    return name=="SYSTEM.ini" || name=="D3D.ini" || name=="Documents/Leaf/WHITE ALBUM2/SYSTEM.ini" || name=="Documents/Leaf/WHITE ALBUM2/D3D.ini";
}
}
