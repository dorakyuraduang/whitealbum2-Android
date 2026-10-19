#include "sdl_movie.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace wa2::platform{
void SdlMovie::draw(const movie::VideoFrame& frame,int logical_width,int logical_height){
    if(logical_width<1 || logical_height<1 || frame.aspect_numerator<1 || frame.aspect_denominator<1)
        throw std::runtime_error("invalid native movie display bounds");
    if(!texture_ || width_!=frame.width || height_!=frame.height){
        texture_=canvas_.upload_yuv(frame);width_=frame.width;height_=frame.height;uploaded_=frame.owner;
    }else if(uploaded_!=frame.owner){canvas_.update_yuv(texture_,frame);uploaded_=frame.owner;}
    const auto aspect=double(frame.width)*frame.aspect_numerator/frame.height/frame.aspect_denominator;
    if(!std::isfinite(aspect) || aspect<=0)throw std::runtime_error("invalid native movie aspect ratio");
    const auto width=std::min(double(logical_width),logical_height*aspect),height=width/aspect;
    canvas_.clip({});canvas_.clear({0,0,0,255});
    canvas_.copy_yuv(texture_,frame,float((logical_width-width)/2),float((logical_height-height)/2),float(width),float(height));
}
}
