#include "winrecomp/native_memory.hpp"
#include <cstring>
#include <new>
#include <stdexcept>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif
namespace wr {
namespace {
constexpr std::size_t PageSize=4096,AddressSpace=0x100000000ull,GuardSize=65536;
// Executable guest pages hold immutable input data. They need host read access
// for code verification; native execution jumps only into generated host code.
#if defined(_WIN32)
DWORD protection(unsigned flags){return flags&2?PAGE_READWRITE:flags&(1|4)?PAGE_READONLY:PAGE_NOACCESS;}
#else
int protection(unsigned flags){return (flags&(1|4)?PROT_READ:0)|(flags&2?PROT_WRITE:0);}
#endif
}
std::unique_ptr<NativeMemoryArena> NativeMemoryArena::create() noexcept {
    constexpr auto size=AddressSpace+GuardSize;
#if defined(_WIN32)
    auto* memory=static_cast<std::uint8_t*>(::VirtualAlloc(nullptr,size,MEM_RESERVE,PAGE_NOACCESS));
    if(!memory)return {};
#else
    // Guest page permissions are 4 KiB. A host with larger protection pages
    // cannot safely expose this particular linear-memory backend; Memory's
    // portable page storage remains available without weakening permissions.
    if(::sysconf(_SC_PAGESIZE)!=PageSize)return {};
    auto* memory=static_cast<std::uint8_t*>(::mmap(nullptr,size,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
    if(memory==MAP_FAILED)return {};
#endif
    auto* result=new(std::nothrow) NativeMemoryArena(memory,size);
    if(!result){
#if defined(_WIN32)
        ::VirtualFree(memory,0,MEM_RELEASE);
#else
        ::munmap(memory,size);
#endif
    }
    return std::unique_ptr<NativeMemoryArena>(result);
}
NativeMemoryArena::~NativeMemoryArena(){
#if defined(_WIN32)
    ::VirtualFree(base_,0,MEM_RELEASE);
#else
    ::munmap(base_,reservation_);
#endif
}
std::uint8_t* NativeMemoryArena::commit(std::uint32_t at,unsigned permissions){
    return commit_range(at,PageSize,permissions);
}
std::uint8_t* NativeMemoryArena::commit_range(std::uint32_t at,std::size_t bytes,unsigned permissions){
    if(at%PageSize || !bytes || bytes%PageSize || std::uint64_t(at)+bytes>AddressSpace)throw std::runtime_error("invalid native memory range");
#if defined(_WIN32)
    if(::VirtualAlloc(base_+at,bytes,MEM_COMMIT,protection(permissions))!=base_+at)
        throw std::bad_alloc();
#else
    if(::mprotect(base_+at,bytes,protection(permissions)))throw std::bad_alloc();
#endif
    ++statistics_.commits;statistics_.committed_pages+=bytes/PageSize;
    return base_+at;
}
void NativeMemoryArena::decommit(std::uint32_t at) noexcept {
    decommit_range(at,PageSize);
}
void NativeMemoryArena::decommit_range(std::uint32_t at,std::size_t bytes) noexcept {
    ++statistics_.decommits;statistics_.decommitted_pages+=bytes/PageSize;
#if defined(_WIN32)
    ::VirtualFree(base_+at,bytes,MEM_DECOMMIT);
#else
    ::mprotect(base_+at,bytes,PROT_NONE);::madvise(base_+at,bytes,MADV_DONTNEED);
#endif
}
void NativeMemoryArena::protect(std::uint32_t at,unsigned permissions){
#if defined(_WIN32)
    DWORD old{};if(!::VirtualProtect(base_+at,PageSize,protection(permissions),&old))throw std::runtime_error("native page protection failed");
#else
    if(::mprotect(base_+at,PageSize,protection(permissions)))throw std::runtime_error("native page protection failed");
#endif
}
void NativeMemoryArena::initialize(std::uint32_t page,std::size_t offset,std::span<const std::uint8_t> data,unsigned permissions){
    if(offset>PageSize || data.size()>PageSize-offset)throw std::runtime_error("native initializer crosses page");
    if(!(permissions&2))protect(page,1|2);
    std::memmove(base_+page+offset,data.data(),data.size());
    if(!(permissions&2))protect(page,permissions);
}
}
