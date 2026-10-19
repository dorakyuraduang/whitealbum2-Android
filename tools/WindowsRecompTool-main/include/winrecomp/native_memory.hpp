#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
namespace wr {
// One sparse 32-bit virtual address space. Reservation consumes address space;
// pages consume physical memory only after commit. It never executes PE bytes.
class NativeMemoryArena {
public:
    struct Statistics {std::uint64_t commits{},decommits{},committed_pages{},decommitted_pages{};};
private:
    std::uint8_t* base_{};
    std::size_t reservation_{};
    Statistics statistics_;
    NativeMemoryArena(std::uint8_t* base,std::size_t size):base_(base),reservation_(size){}
public:
    static std::unique_ptr<NativeMemoryArena> create() noexcept;
    ~NativeMemoryArena();
    NativeMemoryArena(const NativeMemoryArena&)=delete;
    NativeMemoryArena& operator=(const NativeMemoryArena&)=delete;
    std::uint8_t* base() const noexcept{return base_;}
    Statistics statistics() const noexcept{return statistics_;}
    std::uint8_t* commit(std::uint32_t at,unsigned permissions);
    std::uint8_t* commit_range(std::uint32_t at,std::size_t bytes,unsigned permissions);
    void decommit(std::uint32_t at) noexcept;
    void decommit_range(std::uint32_t at,std::size_t bytes) noexcept;
    void protect(std::uint32_t at,unsigned permissions);
    void initialize(std::uint32_t page,std::size_t offset,std::span<const std::uint8_t> data,unsigned permissions);
};
}
