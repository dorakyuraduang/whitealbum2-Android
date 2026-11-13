#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
namespace wr {class Process;}
namespace wa2::platform::compat {
using IoDiagnosticSink=std::function<void(std::string_view,std::string_view)>;
using IoDiagnosticClock=std::function<std::uint64_t()>;
// Small private progress records at ordinary host poll boundaries. No guest
// execution, screen access, or selected-directory writes occur here.
void install_io_diagnostics(wr::Process&,const std::filesystem::path& final_report,
    std::string release,IoDiagnosticSink={},IoDiagnosticClock={});
// Emit once when the shared game thread finishes. No directory/file access;
// sequence 65 follows the at-most-64 I/O-triggered progress snapshots.
void emit_final_io_counters(wr::Process&,std::string_view release,const IoDiagnosticSink&,bool source_returned) noexcept;
}
