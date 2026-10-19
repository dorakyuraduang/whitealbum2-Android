#pragma once
#include "../render/surface_host.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
namespace wa2::window {
// SDL events use window units, while presentation uses drawable pixels. Keep
// the game's render extent independent from the source client-coordinate ABI.
struct PointerCoordinates {
    int window_width{},window_height{},drawable_width{},drawable_height{};
    std::uint32_t source_width{},source_height{};
    std::array<int,4> content{};
    std::array<int,4> client_content{};
    static PointerCoordinates from_client(int ww,int wh,int dw,int dh,const graphics::ClientViewport& viewport){
        PointerCoordinates result;result.window_width=ww;result.window_height=wh;result.drawable_width=dw;result.drawable_height=dh;
        if(viewport.size[0]>0 && viewport.size[1]>0){result.source_width=std::uint32_t(viewport.size[0]);result.source_height=std::uint32_t(viewport.size[1]);}
        result.content=viewport.drawable;result.client_content={0,0,viewport.size[0],viewport.size[1]};return result;
    }
    bool valid()const{return window_width>0 && window_height>0 && drawable_width>0 && drawable_height>0 && source_width && source_height && content[2]>0 && content[3]>0;}
    std::array<int,4> client_rectangle()const{return client_content[2]>0 && client_content[3]>0?client_content:std::array<int,4>{0,0,int(source_width),int(source_height)};}
    std::optional<std::array<std::int32_t,2>> source(double x,double y)const{
        if(!valid() || !std::isfinite(x) || !std::isfinite(y))return {};
        const auto client=client_rectangle();
        // Present scales render pixels into the host drawable. The source
        // program polls CLIENT pixels and performs its own game-space mapping.
        // Returning render pixels here scales a second time when the two
        // extents differ (e.g. 1920x1080 buffer, 1280x720 source window).
        const double sx=client[0]+(x*drawable_width/window_width-content[0])*client[2]/content[2];
        const double sy=client[1]+(y*drawable_height/window_height-content[1])*client[3]/content[3];
        // Preserve negative/outside coordinates: black bars must not become
        // clicks on the first or last row of the game canvas.
        return std::array<std::int32_t,2>{std::int32_t(std::floor(std::clamp(sx,-32768.,32767.))),std::int32_t(std::floor(std::clamp(sy,-32768.,32767.)))};
    }
    std::optional<std::array<int,2>> window(std::int32_t x,std::int32_t y)const{
        if(!valid())return {};
        const auto client=client_rectangle();
        return std::array<int,2>{int(std::lround((content[0]+double(x-client[0])*content[2]/client[2])*window_width/drawable_width)),
                                 int(std::lround((content[1]+double(y-client[1])*content[3]/client[3])*window_height/drawable_height))};
    }
};
inline std::uint32_t packed_pointer(std::array<std::int32_t,2> point){return (std::uint32_t(point[0])&65535u)|((std::uint32_t(point[1])&65535u)<<16);}
}
