#include "image_reference.hpp"
#include "checkpoint_codec.hpp"
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
}
