#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include "integrity.hpp"
#include <algorithm>
#include <array>

namespace wa2::paktool {
struct Sha256::State {
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE hash{};
    std::vector<std::uint8_t> object;
    bool finished{};
    ~State() {
        if(hash) BCryptDestroyHash(hash);
        if(algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    }
};
namespace {
void checked(NTSTATUS status) {
    if(status < 0) throw Error("SHA-256 operation failed (Windows status " + std::to_string(std::uint32_t(status)) + ")");
}
}
Sha256::Sha256() : state_(std::make_unique<State>()) {
    checked(BCryptOpenAlgorithmProvider(&state_->algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0));
    ULONG bytes{}, returned{};
    checked(BCryptGetProperty(state_->algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&bytes), sizeof(bytes), &returned, 0));
    if(returned != sizeof(bytes) || bytes > 1024*1024) throw Error("Invalid SHA-256 provider object size");
    state_->object.resize(bytes);
    checked(BCryptCreateHash(state_->algorithm, &state_->hash, state_->object.data(), bytes, nullptr, 0, 0));
}
Sha256::~Sha256() = default;
void Sha256::update(std::span<const std::uint8_t> bytes) {
    if(state_->finished) throw Error("SHA-256 digest is already finalized");
    while(!bytes.empty()) {
        const auto count = std::min<std::size_t>(bytes.size(), 1024*1024);
        checked(BCryptHashData(state_->hash, const_cast<PUCHAR>(bytes.data()), ULONG(count), 0));
        bytes = bytes.subspan(count);
    }
}
std::string Sha256::finish() {
    if(state_->finished) throw Error("SHA-256 digest is already finalized");
    std::array<UCHAR, 32> hash{};
    checked(BCryptFinishHash(state_->hash, hash.data(), ULONG(hash.size()), 0)); state_->finished = true;
    constexpr char digits[] = "0123456789abcdef";
    std::string result; result.reserve(64);
    for(const auto byte : hash) { result += digits[byte >> 4]; result += digits[byte & 15]; }
    return result;
}
ReadLock::ReadLock(const fs::path& path) {
    handle_ = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if(handle_ == INVALID_HANDLE_VALUE) {
        handle_ = nullptr;
        throw Error(ErrorKind::read, "Cannot open a stable read-only input (Windows error " + std::to_string(GetLastError()) + "): " + utf8(path));
    }
}
ReadLock::~ReadLock() { if(handle_) CloseHandle(handle_); }
}
