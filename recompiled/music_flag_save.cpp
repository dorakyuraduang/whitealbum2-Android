#include "music_flag_save.hpp"
#include <cerrno>
#include <stdexcept>

namespace wa2::aot {namespace {
struct ErrorScope {int value{errno};~ErrorScope(){errno=value;}};
}
MusicFlagSaves::Snapshot MusicFlagSaves::capture(wr::Memory& memory){
    Snapshot result;memory.copy_out(0x521930,result.flags);
    unsigned n{};for(;n<result.path.size();++n){result.path[n]=std::uint8_t(memory.load(0x4bf530+n,8));if(!result.path[n])break;}
    if(!n || n==result.path.size())throw std::runtime_error("Unbounded music system-save path");
    return result;
}
bool MusicFlagSaves::prepare(wr::Cpu& cpu,wr::Memory& memory,MusicFlagStampReader reader,void* owner)noexcept{
    ErrorScope error;pending_=false;
    try{
        const auto sp=cpu.r[wr::ESP];
        // Only the cached music registration caller is admitted. Script flags,
        // explicit saves, other writers and workers retain their normal I/O.
        if(std::uint64_t(sp)+40>0x100000000ull || memory.load(sp+28,32)!=0x40fb98 ||
           memory.load(sp+32,32)<100 || memory.load(sp+32,32)>299 || memory.load(sp+36,32)!=1 ||
           memory.load(sp,32)!=0x4bf530 || memory.load(sp+4,32)!=0x521930 ||
           memory.load(sp+8,32)!=0x268480 || memory.load(sp+12,32)!=4096){++declined_;valid_=false;return false;}
        const auto stamp=reader?reader(owner):std::nullopt;
        if(!stamp || !stamp->files){++declined_;valid_=false;return false;}
        candidate_=capture(memory);
        if(valid_ && saved_stamp_==*stamp && saved_==candidate_){++skipped_;return true;}
        valid_=false;pending_=true;closed_=false;stack_=sp;++attempts_;return false;
    }catch(...){++declined_;valid_=false;return false;}
}
void MusicFlagSaves::closed(const wr::Cpu& cpu,wr::Memory& memory)noexcept{
    if(!pending_)return;ErrorScope error;
    try{const auto sp=cpu.r[wr::ESP];
        if(std::uint64_t(sp)+12!=stack_ || memory.load(sp+8,32)!=0x405361)return;
        closed_=cpu.r[wr::EAX]!=0 && memory.load(sp+4,32)==4096;
    }catch(...){closed_=false;}
}
void MusicFlagSaves::complete(const wr::Cpu& cpu,wr::Memory& memory,MusicFlagStampReader reader,void* owner)noexcept{
    if(!pending_ || cpu.r[wr::ESP]!=stack_)return;pending_=false;ErrorScope error;
    try{
        const auto stamp=reader?reader(owner):std::nullopt;
        if(!closed_ || cpu.r[wr::EAX]!=4096 || !stamp || !stamp->files || candidate_!=capture(memory))return;
        saved_=candidate_;saved_stamp_=*stamp;valid_=true;++committed_;
    }catch(...){valid_=false;}
}
std::string MusicFlagSaves::report()const{
    return "{\"write_attempts\":"+std::to_string(attempts_)+",\"committed\":"+std::to_string(committed_)+
        ",\"duplicate_writes_avoided\":"+std::to_string(skipped_)+",\"declined\":"+std::to_string(declined_)+
        ",\"flags_bytes\":4096,\"only_music_registration\":true,\"successful_close_required\":true}";
}
bool source_music_flags_write(wr::Cpu& cpu,wr::Memory& memory)noexcept{
    const auto& p=current_music_flags;return p.saves && p.memory==&memory && p.saves->prepare(cpu,memory,p.stamp,p.owner);
}
void source_music_flags_closed(const wr::Cpu& cpu,wr::Memory& memory)noexcept{
    const auto& p=current_music_flags;if(p.saves && p.memory==&memory)p.saves->closed(cpu,memory);
}
void source_music_flags_complete(const wr::Cpu& cpu,wr::Memory& memory)noexcept{
    const auto& p=current_music_flags;if(p.saves && p.memory==&memory)p.saves->complete(cpu,memory,p.stamp,p.owner);
}
}
