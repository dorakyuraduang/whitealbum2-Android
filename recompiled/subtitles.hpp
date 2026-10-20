#pragma once
#include "../engine/subtitles.hpp"
#include "subtitle_events.hpp"
#include <memory>

namespace wa2::aot {
// A native sidecar owned by the process's existing shutdown closures. Keep
// Runtime/Process layouts identical to the saved ABI comparison archives.
std::shared_ptr<subtitles::Playback> bind_source_subtitles(wr::Memory&);
std::shared_ptr<subtitles::Playback> find_source_subtitles(wr::Memory&);
void unbind_source_subtitles(wr::Memory&,const subtitles::Playback*) noexcept;
// The stack points at the first cdecl argument after the original epilogue.
// The observer executes only after the source RET itself has succeeded.
void source_subtitle_return(subtitles::Playback&,std::uint32_t entry,
    const wr::Cpu&,wr::Memory&,std::uint32_t arguments,std::uint64_t now);
}
