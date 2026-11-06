#pragma once
#include "image_colours.hpp"
#include <string>

namespace wa2::image {
struct Reference {std::string archive,member;ImageStyle style;};
// Names address external PAK members, never filesystem paths or pixel dumps.
void validate_reference(const Reference&);
std::vector<std::uint8_t> checkpoint_reference(const Reference&);
Reference restore_reference(std::span<const std::uint8_t>);
}
