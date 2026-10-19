#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <stdexcept>
namespace wa2::platform::compat {
struct Codepage {std::uint32_t number;std::span<const std::uint16_t> single;std::span<const std::uint32_t> pairs,reverse;};
const Codepage* codepage(std::uint32_t) noexcept;
class EncodingError:public std::runtime_error {using std::runtime_error::runtime_error;};
std::u16string decode_text(std::uint32_t,std::span<const std::uint8_t>,bool strict=false);
struct Encoded {std::vector<std::uint8_t> bytes;bool substituted{};};
Encoded encode_text(std::uint32_t,std::u16string_view,std::span<const std::uint8_t> fallback={},bool strict=false);
std::uint16_t character_type(char16_t,std::uint32_t type);
char16_t character_case(char16_t,bool upper);
}
