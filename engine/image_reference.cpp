#include "image_reference.hpp"
#include "checkpoint_codec.hpp"
#include "resource_catalog.hpp"
#include <algorithm>

namespace wa2::image {
void validate_reference(const Reference& value){
    validate_image_style(value.style);std::string_view archive=value.archive;
    if(archive.starts_with("IC/"))archive.remove_prefix(3);
    if(!archive.ends_with(".pak"))throw Error("Saved image archive must be a PAK identifier");archive.remove_suffix(4);
    if(archive.empty() || archive.size()>64)throw Error("Saved image archive identifier exceeds limits");
    for(auto c:archive)if(!((c>='a' && c<='z') || (c>='0' && c<='9') || c=='_' || c=='-'))throw Error("Invalid saved image archive identifier");
    if(value.member.empty() || value.member.size()>255 || value.member.find('\0')!=std::string::npos || value.member.find('/')!=std::string::npos || value.member.find('\\')!=std::string::npos)
        throw Error("Invalid saved image member name");
}
std::vector<std::uint8_t> checkpoint_reference(const Reference& value){
    validate_reference(value);checkpoint::Writer out;out.string(value.archive);out.string(value.member);out.i32(value.style.chroma);out.boolean(bool(value.style.colours));
    if(value.style.colours)for(const auto& channel:value.style.colours->channels)out.raw(channel);
    return out.seal("LSIM");
}
Reference restore_reference(std::span<const std::uint8_t> bytes){
    auto in=checkpoint::Reader::open("LSIM",bytes);Reference value;value.archive=in.string(71);value.member=in.string(255);value.style.chroma=in.i32();
    if(in.boolean()){auto map=std::make_shared<ColourMap>();for(auto& channel:map->channels){const auto bytes=in.raw(channel.size());std::copy(bytes.begin(),bytes.end(),channel.begin());}value.style.colours=std::move(map);}
    in.finish();validate_reference(value);return value;
}
std::shared_ptr<const Raster> resolve_reference(const Reference& reference,const resource::Catalog& resources,const Resolve& resolve){
    validate_reference(reference);if(!resolve)throw Error("Missing saved image resolver");auto raster=resolve(reference);
    if(!raster || !raster->width || !raster->height || raster->width>65535 || raster->height>65535 || std::uint64_t(raster->width)*raster->height*4!=raster->rgba.size())throw Error("Saved image resolver returned invalid pixels");
    if(checkpoint_reference(resources.reference(raster))!=checkpoint_reference(reference))throw Error("Saved image resolver returned a different resource");return raster;
}
std::vector<std::uint8_t> checkpoint_image(const resource::Catalog& resources,const std::shared_ptr<const Raster>& raster){
    if(!raster || !raster->width || !raster->height)throw Error("Missing saved image pixels");checkpoint::Writer out;out.u32(raster->width);out.u32(raster->height);out.blob(checkpoint_reference(resources.reference(raster)));return out.seal("LSRA");
}
std::shared_ptr<const Raster> restore_image(std::span<const std::uint8_t> bytes,const resource::Catalog& resources,const Resolve& resolve){
    auto in=checkpoint::Reader::open("LSRA",bytes);const auto width=in.u32(),height=in.u32();if(!width || !height || width>65535 || height>65535)throw Error("Invalid saved image dimensions");
    auto reference=restore_reference(in.blob(2048));in.finish();auto raster=resolve_reference(reference,resources,resolve);if(raster->width!=width || raster->height!=height)throw Error("Saved image dimensions changed");return raster;
}
}
