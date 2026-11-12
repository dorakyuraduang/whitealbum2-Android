#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iosfwd>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
namespace wr {class Process;}
namespace wa2::platform::compat {
// ANSI profile bytes remain in their original encoding. Section/key matching
// only folds ASCII; neither the OS locale nor a Unicode conversion changes data.
class Profile {
    struct Entry {std::string key,value;};
    struct Section {std::string name;std::vector<Entry> entries;};
    std::vector<Section> sections_;
public:
    static Profile read(const std::filesystem::path&);
    static Profile read(std::span<const std::uint8_t>);
    std::optional<std::string> get(std::string_view section,std::string_view key) const;
    std::vector<std::string> sections() const;
    std::vector<std::string> keys(std::string_view section) const;
    void set(std::string section,std::optional<std::string> key,std::optional<std::string> value);
    void save(const std::filesystem::path&) const;
    std::vector<std::uint8_t> bytes()const;
private:
    static Profile parse(std::istream&);
};
Profile read_profile(wr::Process&,const std::filesystem::path&);
void save_profile(wr::Process&,const Profile&,const std::filesystem::path&);
using ProfileReadScope=std::function<std::optional<std::uint64_t>(wr::Process&)>;
using ProfileWriteScope=ProfileReadScope;
// Read-only snapshots for a caller-defined configuration batch. Explicit
// writes are never delayed; storage revisions and ordinary callers invalidate.
class ProfileReader {
    struct State;std::shared_ptr<State> state_;
public:
    explicit ProfileReader(ProfileReadScope={});
    Profile read(wr::Process&,const std::filesystem::path&);
    void clear();
    std::string report()const;
};
// Only a caller-certified source sequence may reuse its most recently loaded
// or committed profile and omit an unchanged value. Changed values are always
// published before returning. Ordinary API writes retain fresh read/write I/O.
class ProfileWriter {
    struct State;std::shared_ptr<State> state_;
public:
    explicit ProfileWriter(ProfileWriteScope={});
    void write(wr::Process&,const std::filesystem::path&,std::string,std::optional<std::string>,std::optional<std::string>);
    void clear();
    std::string report()const;
};
std::string encode_profile_struct(std::span<const std::uint8_t>);
bool decode_profile_struct(std::string_view,std::span<std::uint8_t>);
std::uint32_t profile_integer(std::string_view,std::uint32_t fallback);
}
