#include "images.hpp"
#include "image_pixel_profile.hpp"
namespace wa2::special {
bool image_chroma(wr::Cpu& cpu,wr::Memory& memory){return aot::native_image_chroma(cpu,memory,image_chroma_source);}
bool image_colour(wr::Cpu& cpu,wr::Memory& memory){return aot::native_image_colours(cpu,memory,image_colour_source);}
std::string image_pixel_report(){
    const auto c=aot::image_chroma_counts();const auto t=aot::image_colour_counts();
    return "{\"chroma_attempts\":"+std::to_string(c.attempts)+",\"chroma_completed\":"+std::to_string(c.completed)+
        ",\"chroma_pixels\":"+std::to_string(c.pixels)+",\"grey_pixels\":"+std::to_string(c.grey_pixels)+
        ",\"colour_attempts\":"+std::to_string(t.attempts)+",\"colour_completed\":"+std::to_string(t.completed)+
        ",\"colour_pixels\":"+std::to_string(t.pixels)+",\"shared_main_implementation\":true}";
}
}
