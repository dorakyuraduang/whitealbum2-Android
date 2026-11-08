#pragma once
#include "../native_menu_adapter.hpp"
namespace wa2::platform {
std::unique_ptr<NativeMenuAdapter> windows_native_menu(SDL_Window*,std::function<void()> suspend);
}
