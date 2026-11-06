#pragma once
#include "image_colours.hpp"
#include <string>
#include <functional>

namespace wa2::resource {class Catalog;}

namespace wa2::image {
struct Reference {std::string archive,member;ImageStyle style;};
using Resolve=std::function<std::shared_ptr<const Raster>(const Reference&)>;
// Names address external PAK members, never filesystem paths or pixel dumps.
void validate_reference(const Reference&);
std::vector<std::uint8_t> checkpoint_reference(const Reference&);
Reference restore_reference(std::span<const std::uint8_t>);
std::shared_ptr<const Raster> resolve_reference(const Reference&,const resource::Catalog&,const Resolve&);
std::vector<std::uint8_t> checkpoint_image(const resource::Catalog&,const std::shared_ptr<const Raster>&);
std::shared_ptr<const Raster> restore_image(std::span<const std::uint8_t>,const resource::Catalog&,const Resolve&);
}
