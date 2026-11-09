#pragma once
#include <winrecomp/runtime.hpp>
#include <array>
#include <optional>
#include <string>

namespace wa2::aot {
// The title's extras update requests BGM 31 every frame. Keep the source flag
// setter (including its plugin notification), but acknowledge an identical
// flags block only after its own successful write AND CloseHandle.
struct MusicFlagStamp {
    const void* files{};std::uint64_t revision{};
    bool operator==(const MusicFlagStamp&)const=default;
};
using MusicFlagStampReader=std::optional<MusicFlagStamp>(*)(void*);
class MusicFlagSaves {
    struct Snapshot {
        std::array<std::uint8_t,4096> flags{};
        std::array<std::uint8_t,260> path{};
        bool operator==(const Snapshot&)const=default;
    };
    Snapshot saved_,candidate_;
    MusicFlagStamp saved_stamp_{};
    wr::U32 stack_{};
    bool valid_{},pending_{},closed_{};
    std::uint64_t attempts_{},committed_{},skipped_{},declined_{};
    static Snapshot capture(wr::Memory&);
public:
    bool prepare(wr::Cpu&,wr::Memory&,MusicFlagStampReader,void*)noexcept;
    void closed(const wr::Cpu&,wr::Memory&)noexcept;
    void complete(const wr::Cpu&,wr::Memory&,MusicFlagStampReader,void*)noexcept;
    std::string report()const;
};
struct MusicFlagPolicy {MusicFlagSaves* saves{};wr::Memory* memory{};MusicFlagStampReader stamp{};void* owner{};};
inline thread_local MusicFlagPolicy current_music_flags{};
class MusicFlagScope {
    MusicFlagPolicy previous_{current_music_flags};
public:
    MusicFlagScope(MusicFlagSaves& saves,wr::Memory& memory,MusicFlagStampReader stamp,void* owner){current_music_flags={&saves,&memory,stamp,owner};}
    ~MusicFlagScope(){current_music_flags=previous_;}
    MusicFlagScope(const MusicFlagScope&)=delete;
};
bool source_music_flags_write(wr::Cpu&,wr::Memory&)noexcept;
void source_music_flags_closed(const wr::Cpu&,wr::Memory&)noexcept;
void source_music_flags_complete(const wr::Cpu&,wr::Memory&)noexcept;
}
