#pragma once
#include "pak.hpp"
#include <memory>
#include <span>

namespace wa2::paktool {
// Streaming SHA-256 and a read-only file lock live in the Windows adapter.
class Sha256 {
    struct State;
    std::unique_ptr<State> state_;
public:
    Sha256();
    ~Sha256();
    void update(std::span<const std::uint8_t>);
    std::string finish();
};
class ReadLock {
    void* handle_{};
public:
    explicit ReadLock(const fs::path&);
    ~ReadLock();
    ReadLock(const ReadLock&) = delete;
    ReadLock& operator=(const ReadLock&) = delete;
};
}
