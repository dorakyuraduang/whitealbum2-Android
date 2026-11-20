#include "lzss.hpp"
#include "../../engine/resource_archive.hpp"
#include <array>
#include <chrono>
#include <sstream>
#include <vector>
namespace wa2::special {namespace {
constexpr wr::U32 Ring=0xa79d10,Maximum=64u*1024*1024;
struct Counts {std::uint64_t attempts{},completed{},input{},output{},ns{};};
thread_local Counts counts;
bool overlap(wr::U32 a,std::uint64_t n,wr::U32 b,std::uint64_t m){return std::uint64_t(a)<std::uint64_t(b)+m && std::uint64_t(b)<std::uint64_t(a)+n;}
bool writable(wr::Memory& memory,wr::U32 address,unsigned bytes){
    memory.check(address,bytes,wr::Memory::Write);
    for(std::uint64_t at=address;at<std::uint64_t(address)+bytes;at=(at/4096+1)*4096)
        if(memory.permissions(wr::U32(at))&wr::Memory::Execute)return false;
    return true;
}
// This scan proves a complete, exact-size member and remembers the source
// parameter slot reused by the unchanged original inner match loop.
bool layout(std::span<const std::uint8_t> data,unsigned size,unsigned& last_match){
    unsigned at{},written{},flags{};last_match=0;
    while(written<size){
        if(!(flags&0x100)){if(at==data.size())return false;flags=data[at++]|0xff00;}
        if(flags&1){if(at==data.size())return false;++at;++written;}
        else{if(data.size()-at<2)return false;const auto length=(data[at+1]&15)+3;at+=2;
            if(length>size-written)return false;written+=length;last_match=at;}
        flags>>=1;
    }
    return at==data.size();
}
}
bool try_lzss_prefix(wr::Cpu& cpu,wr::Memory& memory){
    if(!lzss_acceleration_enabled)return false;
    ++counts.attempts;
    const auto begun=std::chrono::steady_clock::now();
    try{
        auto scope=memory.execution_scope();
        const auto frame=cpu.r[wr::EBP],source=cpu.r[wr::EBX],target=cpu.r[wr::EDI];
        const auto input=memory.load(frame+0x10,32),output=memory.load(frame+0x14,32);
        if(input>Maximum || output>Maximum || frame<32 || frame>0xffffffe7u || cpu.r[wr::ESI]!=0xfee ||
            std::uint64_t(source)+input>0x100000000ull || std::uint64_t(target)+output>0x100000000ull ||
            overlap(source,input,target,output) || overlap(source,input,Ring,4096) || overlap(target,output,Ring,4096) ||
            overlap(source,input,frame-32,64) || overlap(target,output,frame-32,64))return false;
        for(const auto delta:{4u,8u,12u,16u})if(memory.load(frame-delta,32))return false;
        memory.check(source,input,wr::Memory::Read);memory.check(Ring,4096,wr::Memory::Read);
        if(!writable(memory,target,output) || !writable(memory,Ring,4096) || !writable(memory,frame-16,28))return false;
        std::vector<std::uint8_t> encoded(input),decoded(output);std::array<std::uint8_t,4096> ring;
        memory.copy_out(source,encoded);memory.copy_out(Ring,ring);
        unsigned last_match{};if(!layout(encoded,output,last_match))return false;
        const auto flags=resource::decode_lzss_dictionary(encoded,decoded,ring);
        // Every guest write has been admitted before any guest state changes.
        // Resume the original bounds check, debug trace and epilogue unchanged.
        memory.copy_in(target,decoded);memory.copy_in(Ring,ring);
        memory.store(frame-12,input,32);memory.store(frame-8,flags,32);memory.store(frame-4,output,32);
        if(last_match)memory.store(frame+8,source+last_match,32);
        cpu.r[wr::EBX]=source+input;cpu.r[wr::EDI]=target+output;cpu.r[wr::ESI]=0xfeeu+output;
        ++counts.completed;counts.input+=input;counts.output+=output;
        counts.ns+=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-begun).count());
        return true;
    }catch(const wr::GuestFault&){return false;}catch(const resource::Error&){return false;}catch(const std::bad_alloc&){return false;}
}
std::uint64_t lzss_activity(){return counts.completed;}
std::string lzss_report(){std::ostringstream out;out<<"{\"backend\":\"portable-cpp-special-lzss\",\"attempts\":"<<counts.attempts<<",\"completed\":"<<counts.completed
    <<",\"input_bytes\":"<<counts.input<<",\"decoded_bytes\":"<<counts.output<<",\"decode_ns\":"<<counts.ns<<",\"payload_cached\":false,\"original_tail_preserved\":true}";return out.str();}
}
