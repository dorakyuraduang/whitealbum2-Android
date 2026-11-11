#pragma once
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace wa2::paktool {
namespace fs = std::filesystem;
inline constexpr const char* manifest_name = ".wa2pak-manifest";
enum class Format { kcap, lac, movie };
struct Error : std::runtime_error { using std::runtime_error::runtime_error; };
struct Cancelled : Error { Cancelled() : Error("操作已取消") {} };
struct Options {
    // Zero preserves a manifest's encoding; new folders use Shift-JIS (932).
    unsigned codepage{};
    bool compress = true;
    // Extraction is streamed, but bound total expansion against damaged input.
    std::uint64_t maximum_expanded = 64ull * 1024 * 1024 * 1024;
    const std::atomic_bool* cancel{};
    std::function<void(unsigned, std::string_view)> progress;
};
struct Result {
    fs::path output;
    Format format{};
    std::uint64_t files{}, bytes{};
};
// Both destinations must be new. Neither operation replaces existing data.
Result pack(const fs::path& folder, const fs::path& destination, const Options& = {});
Result unpack(const fs::path& archive, const fs::path& destination, const Options& = {});
std::string utf8(const fs::path&);
std::string decode_name(std::string_view bytes, unsigned codepage);
std::string encode_name(std::string_view text, unsigned codepage);
std::string path_key(const fs::path&);
bool is_link(const fs::path&);
void publish_no_replace(const fs::path& source, const fs::path& destination);
std::vector<std::uint8_t> compress_lzss(const std::vector<std::uint8_t>&, const Options&);
}
