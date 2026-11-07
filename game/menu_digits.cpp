#include "menu_digits.hpp"
#include <stdexcept>

namespace wa2::game {
void menu_digits(std::vector<image::Sprite>& out,std::shared_ptr<const image::Raster> atlas,std::string_view text,float x,float y,std::uint8_t opacity,int layer,int order){
    if(!atlas || atlas->width<192 || atlas->height<32 || std::uint64_t(atlas->width)*atlas->height*4!=atlas->rgba.size() || text.size()>256)throw std::runtime_error("Invalid native menu digit atlas or text");
    for(unsigned char value:text)if(value<32 || value>63)throw std::runtime_error("Unsupported native menu digit character");
    for(unsigned char value:text){if(value!=32 && opacity){const auto tile=unsigned(value-32);out.push_back({atlas,x,y,12,16,float(tile%16*12),float(tile/16*16),12,16,255,255,255,opacity});out.back().layer=layer;out.back().order=order;}x+=12;}
}
}
