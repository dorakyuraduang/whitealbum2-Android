#pragma once
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <optional>

namespace wa2::resource {
// No CPU context, legacy address or operating-system handle enters the engine.
class Source {
public:
    virtual ~Source() = default;
    virtual std::uint64_t size() const = 0;
    virtual void read_at(std::uint64_t offset, std::span<std::uint8_t> bytes) = 0;
};
class Error : public std::runtime_error { using std::runtime_error::runtime_error; };
// Missing candidates are recoverable at file/media API boundaries. Keep them
// distinct from permission, read, and archive-format failures.
class NotFound : public Error { using Error::Error; };
struct Member {
    std::string name;
    std::uint64_t offset{};
    std::uint32_t stored_size{};
    std::uint32_t source_index{};
    bool compressed{};
    std::uint8_t dictionary_fill{};
};
struct StoredResource {
    std::vector<std::uint8_t> bytes;
    std::uint32_t expanded_size{};
    bool compressed{};
    std::uint8_t dictionary_fill{};
};
// ASCII folding is intentional: resource names can also contain Shift-JIS bytes.
int compare_names(std::string_view a, std::string_view b) noexcept;
void validate_lzss(std::span<const std::uint8_t> source,std::size_t output_bytes);
// Checks the stream while decoding. On failure output may be partial, but the
// dictionary is unchanged. Loaders can decode into a private allocation and
// publish both results only after success, without a separate validation pass.
unsigned decode_lzss_dictionary(std::span<const std::uint8_t> source,std::span<std::uint8_t> output,
    std::span<std::uint8_t,4096> dictionary);
// Record which of the 18 caller-owned initial tail bytes affect this output.
// The original three-argument ABI remains available to existing callers.
unsigned decode_lzss_dictionary(std::span<const std::uint8_t> source,std::span<std::uint8_t> output,
    std::span<std::uint8_t,4096> dictionary,unsigned* caller_tail_reads);
void finish_lzss_dictionary(std::span<const std::uint8_t> output,std::span<std::uint8_t,4096> dictionary);
void decode_lzss(std::span<const std::uint8_t> source, std::span<std::uint8_t> output,std::uint8_t dictionary_fill=0);
class Archive {
public:
    static Archive open(std::shared_ptr<Source> source);
    std::span<const Member> members() const noexcept { return members_; }
    bool is_kcap() const noexcept { return stored_size_prefix_; }
    std::size_t find(std::string_view name) const noexcept;
    std::size_t find_first(std::string_view name) const noexcept;
    static constexpr std::size_t missing = std::size_t(-1);
    StoredResource read(std::size_t member, std::size_t maximum = 128u*1024*1024) const;
    std::vector<std::uint8_t> load(std::size_t member, std::size_t maximum = 128u*1024*1024,
        std::optional<std::uint8_t> dictionary_fill = {}) const;
    // Uncompressed members stay in the original PAK and are read by offset.
    // Compressed members expand individually into owned memory on open.
    std::shared_ptr<Source> open_member(std::size_t member,std::size_t maximum=128u*1024*1024) const;
private:
    std::shared_ptr<Source> source_;
    std::vector<Member> members_;
    bool stored_size_prefix_{};
};
}
