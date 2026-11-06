#include "image_colours.hpp"
#include <algorithm>

namespace wa2::image {
ColourMap parse_colour_map(std::span<const std::uint8_t> bytes){
    if(bytes.size()!=256 && bytes.size()!=768 && bytes.size()!=1280)throw Error("Native AMP colour map has an unsupported size");
    ColourMap result;
    for(unsigned channel=0;channel<3;++channel)for(unsigned value=0;value<256;++value){
        const auto index=bytes.size()==256?value:(bytes.size()==1280?256:0)+channel*256+value;
        auto mapped=bytes[index];if(bytes.size()==1280)mapped=bytes[mapped];result.channels[channel][value]=mapped;
    }
    return result;
}
void validate_image_style(const ImageStyle& style){if(style.chroma<0 || style.chroma>256)throw Error("Native image chroma is outside 0..256");}
void apply_image_style(Raster& image,const ImageStyle& style){
    validate_image_style(style);
    if(std::uint64_t(image.width)*image.height*4!=image.rgba.size())throw Error("Native colour conversion has invalid RGBA pixels");
    if(style.chroma==256 && !style.colours)return;
    const auto chroma=unsigned(style.chroma);
    for(std::size_t at=0;at<image.rgba.size();at+=4){auto* p=image.rgba.data()+at;
        if(chroma!=256){const auto grey=(unsigned(p[0])*77+unsigned(p[1])*151+unsigned(p[2])*28)/256;
            if(!chroma)p[0]=p[1]=p[2]=std::uint8_t(grey);
            else for(unsigned n=0;n<3;++n)p[n]=std::uint8_t(unsigned(p[n])*chroma/256+grey*(255-chroma)/256);
        }
        if(style.colours)for(unsigned n=0;n<3;++n)p[n]=style.colours->channels[n][p[n]];
    }
}
}
