#pragma once
#include "widgets.hpp"
namespace wa2::window {
// Implemented by the host OS adapter. All dialog/menu definitions and source
// control state are supplied by the common window model.
std::shared_ptr<Widgets> native_widgets();
}
