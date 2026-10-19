#pragma once
#include "widgets.hpp"
namespace wa2::window {
struct SdlHostOptions {
    bool embedded_gl{},mobile{};
    std::shared_ptr<Widgets> widgets;
    std::function<void()> frame_submitted;
};
// The SDL main window, input mapping and frame submission hook are shared on
// desktop and mobile. Only native widget/device details differ.
std::shared_ptr<Host> sdl_host(SdlHostOptions);
}
