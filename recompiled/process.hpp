#pragma once
#include "functions.hpp"
#include <winrecomp/process.hpp>
namespace wa2::graphics {class SurfaceHost;}
namespace wa2::aot {
wr::Image initial_image();
// The entry used by ALL native hosts. Platform shells supply device/window
// adapters only; game control flow remains in the same static function set.
class Runtime {
    std::unique_ptr<Program> program_;
    wr::Process process_;
public:
    explicit Runtime(wr::ProcessOptions options={},wr::ProcessBackends backends={},
                     std::shared_ptr<graphics::SurfaceHost> graphics={});
    wr::Process& process() noexcept{return process_;}
    void load(){process_.load(initial_image());}
    void load(const wr::Image& image){process_.load(image);}
    std::uint32_t run(){return process_.run();}
};
}
