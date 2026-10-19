#pragma once
#include <memory>
namespace wr {class Process;class DirectDrawBackend;}
namespace wa2::window {class Host;}
namespace wa2::platform::compat {
std::unique_ptr<wr::DirectDrawBackend> install_legacy_display(wr::Process&,std::shared_ptr<window::Host>);
}
