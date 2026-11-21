#pragma once
#include "../static_program.hpp"
#include "../../hosts/shared/startup.hpp"
#include "subtitles.hpp"
namespace wa2::special {
wr::Image initial_image();
std::span<const aot::Function> functions() noexcept;
aot::Location locate(std::uint32_t) noexcept;
std::shared_ptr<wr::MutableFiles> save_files(std::shared_ptr<wr::MutableFiles>,wr::Process* =nullptr);
std::string save_files_report(const std::shared_ptr<wr::MutableFiles>&);
std::uint64_t save_files_activity(const std::shared_ptr<wr::MutableFiles>&);
class Runtime {
    std::unique_ptr<aot::StaticProgram> program_;
    bool shared_codecs_;
    bool shared_resources_;
    std::vector<std::uint32_t> host_boundaries_;
    std::shared_ptr<subtitles::Playback> subtitle_player_;
    std::shared_ptr<SubtitleState> subtitle_state_;
    std::function<bool(wr::Cpu&,wr::Memory&,bool)> execute_;
    wr::Process process_;
    aot::StaticProgram& program();
public:
    // Static reference mode is used only by finite, constructed-memory checks.
    Runtime(wr::ProcessOptions options={},wr::ProcessBackends backends={},std::shared_ptr<graphics::SurfaceHost> surface={},bool shared_codecs=true,bool captions_enabled=false,std::shared_ptr<const subtitles::Catalogue> captions={},bool shared_resources=true);
    wr::Process& process(){return process_;}
};
std::uint32_t run(host::Startup);
}
