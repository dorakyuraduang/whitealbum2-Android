#pragma once
#include <string>
namespace wa2::aot {
// Call on the game/render owner thread, at an existing presentation boundary.
// Counts only: no dialogue, guest addresses, resource paths or screen pixels.
std::string text_work_snapshot();
}
