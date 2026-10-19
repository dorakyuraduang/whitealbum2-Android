#pragma once
// Runtime image metadata has no instruction decoder dependency.
#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>
namespace wr {
using Address=std::uint32_t;
struct Section {
    std::string name;
    std::uint32_t rva{},virtual_size{},raw_offset{},raw_size{},characteristics{};
    std::uint64_t span() const;
};
struct Directory {std::uint32_t rva{},size{};};
struct Import {std::string dll,name;Address iat{};std::optional<std::uint16_t> ordinal;};
struct Image {
    std::vector<std::uint8_t> bytes;
    Address base{},entry{};
    std::uint32_t size{},headers_size{};
    std::array<Directory,16> directories{};
    std::vector<Section> sections;
    std::vector<Import> imports;
    std::set<Address> exports,tls_callbacks;
    // A compiled data module may supply its recorded input identity. External
    // parsers leave it empty and retain their normal whole-input validation.
    std::string static_input_identity;
    static Image parse(std::vector<std::uint8_t>);
    static Image load(const std::filesystem::path&);
    std::optional<std::size_t> offset(Address,std::size_t count=1) const;
    std::span<const std::uint8_t> code(Address) const;
    const Import* import_at(Address) const;
    std::vector<std::uint8_t> mapped() const;
    std::string json() const;
};
std::string hex(std::uint64_t);
std::string quote(const std::string&);
void write_file(const std::filesystem::path&,const std::string&);
}
