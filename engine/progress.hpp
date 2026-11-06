#pragma once
#include "progress_layout.hpp"
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

namespace wa2::progress {
using Bytes=std::vector<std::uint8_t>;
class Error : public std::runtime_error {using std::runtime_error::runtime_error;};
enum class Block : unsigned { history, gallery, flags };
std::size_t offset(Block block);
std::size_t size(Block block);

// Owns ordinary engine state. Byte blocks are retained to round-trip unknown
// save fields and write only the affected format block, with no machine ABI.
class State {
    Bytes history_,gallery_;
    std::array<std::uint8_t,layout::system_flags_bytes> flags_{};
    std::array<bool,3> dirty_{true,true,true};
    void clean(Block block);
    friend class Store;
public:
    State();
    static State decode(std::span<const std::uint8_t> bytes);
    Bytes encode() const;
    std::span<const std::uint8_t> block(Block block) const;
    bool dirty(Block block) const;
    std::int32_t flag(std::uint32_t index) const;
    void set_flag(std::uint32_t index,std::int32_t value);
    std::uint8_t gallery(std::uint32_t bank,std::uint32_t index) const;
    void set_gallery(std::uint32_t bank,std::uint32_t index,std::uint8_t value);
    bool read(std::uint32_t script,std::uint32_t line) const;
    void mark_read(std::uint32_t script,std::uint32_t line);
    void unlock_all_for_testing();
};

// Storage is an adapter: missing saves return nullopt; I/O/format failures
// throw. write() must accept exactly one original-format block and preserve
// all other bytes. No path or OS API is part of the engine contract.
class Storage {
public:
    virtual ~Storage()=default;
    virtual std::optional<Bytes> load()=0;
    virtual void write(Block block,std::span<const std::uint8_t> bytes)=0;
};
class Store {
    std::shared_ptr<Storage> storage_;
    State state_;
public:
    explicit Store(std::shared_ptr<Storage> storage);
    State& state() noexcept{return state_;}
    const State& state() const noexcept{return state_;}
    // Failed blocks remain dirty for an explicit retry. Successful blocks
    // are acknowledged individually; repeated flushes perform no writes.
    void flush();
};
}
