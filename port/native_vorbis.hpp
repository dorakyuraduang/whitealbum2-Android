#pragma once
#include <cstdint>
#include <functional>
#include <string_view>
namespace wr {class Process;class Memory;
// A program selects its verified ABI addresses; the stream implementation and
// object layout are common to every program and host architecture.
struct NativeVorbisProfile {
    std::string_view input_sha256;
    std::uint32_t construct{},reset{},format{},decode{};
    std::function<bool(Memory&,std::uint32_t)> verified;
};
// Tests can retain the original complete decoder for a reference comparison.
void install_wa2_native_vorbis(Process&,bool enabled=true);
void install_wa2_native_vorbis(Process&,NativeVorbisProfile,bool enabled=true);
std::uint64_t wa2_native_vorbis_activity(Process&);
}
