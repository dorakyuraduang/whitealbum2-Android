#pragma once
#include "../../platform/window/host.hpp"
#include <winrecomp/process.hpp>
#include <winrecomp/readonly_files.hpp>
#include "../../platform/compat/io_diagnostics.hpp"
namespace wa2::host {
struct Startup {
    wr::ProcessOptions options;
    std::shared_ptr<window::Host> window;
    std::function<void(wr::Process&)> mount_resources;
    std::shared_ptr<wr::MutableFiles> files;
    std::filesystem::path report_file;
    platform::compat::IoDiagnosticSink io_diagnostic_sink;
    bool test_mode{};
};
// This is the only game startup/run path. Platform entry files select device
// adapters and resource access, then enter the same preserved source program.
std::uint32_t run(Startup);
void require_imports(wr::Process&,const wr::Image&);
}
