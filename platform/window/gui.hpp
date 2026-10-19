#pragma once
#include "host.hpp"
#include <winrecomp/process.hpp>
namespace wa2::window {
// Does not create any window until source CreateWindowEx/DialogBox is called.
std::unique_ptr<wr::GuiBackend> install(wr::Process&,std::shared_ptr<Host>);
}
