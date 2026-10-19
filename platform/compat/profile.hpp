#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
namespace wa2::platform::compat {
// ANSI profile bytes remain in their original encoding. Section/key matching
// only folds ASCII; neither the OS locale nor a Unicode conversion changes data.
class Profile {
    struct Entry {std::string key,value;};
    struct Section {std::string name;std::vector<Entry> entries;};
    std::vector<Section> sections_;
public:
    static Profile read(const std::filesystem::path&);
    std::optional<std::string> get(std::string_view section,std::string_view key) const;
    std::vector<std::string> sections() const;
    std::vector<std::string> keys(std::string_view section) const;
    void set(std::string section,std::optional<std::string> key,std::optional<std::string> value);
    void save(const std::filesystem::path&) const;
};
std::string encode_profile_struct(std::span<const std::uint8_t>);
bool decode_profile_struct(std::string_view,std::span<std::uint8_t>);
std::uint32_t profile_integer(std::string_view,std::uint32_t fallback);
}
