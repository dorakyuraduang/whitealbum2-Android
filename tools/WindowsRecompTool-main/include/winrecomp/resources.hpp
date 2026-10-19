#pragma once
#include "winrecomp/image.hpp"
#include <variant>
namespace wr {
// PE resource names are UTF-16 strings or unsigned 16-bit identifiers, NOT VAs.
using ResourceName=std::variant<std::uint16_t,std::u16string>;
struct Resource {
    ResourceName type,name;
    std::uint16_t language{};
    std::uint32_t codepage{},rva{},size{};
};
// Validates the complete three-level directory, with bounded work and no native
// module loading. Returned payloads refer to the immutable original image bytes.
std::vector<Resource> resource_directory(const Image& image);
std::optional<Resource> find_resource(const Image& image,const ResourceName& type,
                                      const ResourceName& name,std::uint16_t language=0x409);
std::span<const std::uint8_t> resource_bytes(const Image& image,const Resource& resource);
}
