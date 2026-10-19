#include "movie_layout.hpp"
#include <algorithm>
namespace wa2::graphics {
std::optional<MovieLayout> layout_movie(const movie::VideoFrame& frame,const MovieView& view,const ClientViewport& viewport){
    const auto& sr=view.source;const auto& dr=view.destination;const auto& bounds=viewport.drawable;
    if(frame.width<1 || frame.height<1 || frame.aspect_numerator<1 || frame.aspect_denominator<1 ||
       viewport.size[0]<1 || viewport.size[1]<1 || bounds[2]<1 || bounds[3]<1 ||
       sr[0]<0 || sr[1]<0 || sr[2]<1 || sr[3]<1 || std::int64_t(sr[0])+sr[2]>frame.width ||
       std::int64_t(sr[1])+sr[3]>frame.height || dr[2]<1 || dr[3]<1)return {};
    const double x=bounds[0]+double(dr[0])*bounds[2]/viewport.size[0];
    const double y=bounds[1]+double(dr[1])*bounds[3]/viewport.size[1];
    const double width=double(dr[2])*bounds[2]/viewport.size[0],height=double(dr[3])*bounds[3]/viewport.size[1];
    const double aspect=double(sr[2])*frame.aspect_numerator/sr[3]/frame.aspect_denominator;
    const double dw=std::min(width,height*aspect),dh=dw/aspect;
    return MovieLayout{{x+(width-dw)/2,y+(height-dh)/2,dw,dh},
        {float(sr[0])/frame.width,float(sr[1])/frame.height,float(sr[0]+sr[2])/frame.width,float(sr[1]+sr[3])/frame.height}};
}
}
