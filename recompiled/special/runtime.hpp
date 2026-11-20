#pragma once
#include "../static_program.hpp"
#include "../../hosts/shared/startup.hpp"
namespace wa2::special {
wr::Image initial_image();
std::span<const aot::Function> functions() noexcept;
aot::Location locate(std::uint32_t) noexcept;
std::shared_ptr<wr::MutableFiles> save_files(std::shared_ptr<wr::MutableFiles>);
std::string save_files_report(const std::shared_ptr<wr::MutableFiles>&);
class Runtime {
    std::unique_ptr<aot::StaticProgram> program_;
    bool shared_codecs_;
    wr::Process process_;
    aot::StaticProgram& program();
public:
    // Static reference mode is used only by finite, constructed-memory checks.
    Runtime(wr::ProcessOptions options={},wr::ProcessBackends backends={},std::shared_ptr<graphics::SurfaceHost> surface={},bool shared_codecs=true);
    wr::Process& process(){return process_;}
};
std::uint32_t run(host::Startup);
}
