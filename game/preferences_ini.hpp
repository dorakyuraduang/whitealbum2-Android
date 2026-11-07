#pragma once
#include "preferences.hpp"

namespace wa2::game {
// Source SYSTEM/D3D integer settings are ordinary file formats. This decoder
// neither calls profile APIs nor keeps source addresses or machine state.
// Empty spans mean that the corresponding optional file was absent.
Preferences import_preferences_ini(std::span<const std::uint8_t> system,
    std::span<const std::uint8_t> display);
}
