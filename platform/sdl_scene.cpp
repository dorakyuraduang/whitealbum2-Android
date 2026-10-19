#include "sdl_scene.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace wa2::platform {
std::shared_ptr<CanvasTexture> SdlScene::texture(const std::shared_ptr<const image::Raster>& image){
    auto found=std::find_if(uploads_.begin(),uploads_.end(),[&](const auto& item){return item.image.lock()==image;});
    if(found==uploads_.end()){uploads_.push_back({image,canvas_.upload(*image)});found=uploads_.end()-1;}
    return found->texture;
}
void SdlScene::draw(const script::BackgroundLayer& layer){
    if(!layer.image || !layer.opacity)return;
    const auto x=layer.x,y=layer.y,right=x+layer.width,bottom=y+layer.height;
    const CanvasColour colour{std::uint8_t(std::clamp(layer.colour[0]*2,0,255)),std::uint8_t(std::clamp(layer.colour[1]*2,0,255)),
        std::uint8_t(std::clamp(layer.colour[2]*2,0,255)),layer.opacity};
    const std::array<CanvasVertex,6> vertices{{{x,y,0,0,colour},{right,y,1,0,colour},{right,bottom,1,1,colour},
        {x,y,0,0,colour},{right,bottom,1,1,colour},{x,bottom,0,1,colour}}};
    canvas_.triangles(texture(layer.image),vertices);
    if(std::any_of(layer.colour.begin(),layer.colour.end(),[](auto channel){return channel>128;})){
        const CanvasColour light{std::uint8_t(std::clamp(layer.colour[0]*2-256,0,255)),std::uint8_t(std::clamp(layer.colour[1]*2-256,0,255)),
            std::uint8_t(std::clamp(layer.colour[2]*2-256,0,255)),layer.opacity};
        auto additive=vertices;for(auto& vertex:additive)vertex.colour=light;canvas_.triangles({},additive,CanvasBlend::add);
    }
}
void SdlScene::sprites(std::span<const image::Sprite> items){
    if(items.size()>65536)throw std::runtime_error("native sprite batch exceeds limit");
    std::erase_if(uploads_,[](const auto& item){return item.image.expired();});
    auto& vertices=sprite_vertices_;vertices.clear();std::shared_ptr<const image::Raster> source;auto blend=image::SpriteBlend::alpha;
    if(vertices.capacity()<items.size()*6)vertices.reserve(items.size()*6);
    const auto flush=[&]{if(!vertices.empty()){canvas_.triangles(texture(source),vertices,blend==image::SpriteBlend::add?CanvasBlend::add:CanvasBlend::alpha);vertices.clear();}};
    for(const auto& item:items){
        if(!item.image || !item.alpha)continue;
        if(!item.image->width || !item.image->height || item.width<0 || item.height<0 || item.source_x<0 || item.source_y<0 ||
            item.source_width<0 || item.source_height<0 || item.source_x+item.source_width>item.image->width ||
            item.source_y+item.source_height>item.image->height)throw std::runtime_error("native sprite source is out of bounds");
        if(source!=item.image || blend!=item.blend){flush();source=item.image;blend=item.blend;}
        const auto x=item.x,y=item.y,right=x+item.width,bottom=y+item.height;
        auto u=item.source_x/source->width,v=item.source_y/source->height;
        auto u1=(item.source_x+item.source_width)/source->width,v1=(item.source_y+item.source_height)/source->height;
        if(item.flip&1)std::swap(u,u1);if(item.flip&2)std::swap(v,v1);
        const CanvasColour colour{item.red,item.green,item.blue,item.alpha};
        std::array<CanvasVertex,6> quad{{{x,y,u,v,colour},{right,y,u1,v,colour},{right,bottom,u1,v1,colour},
            {x,y,u,v,colour},{right,bottom,u1,v1,colour},{x,bottom,u,v1,colour}}};
        if(item.angle){const auto radians=double(item.angle)*.01745329238474369;const auto c=float(std::cos(radians)),s=float(std::sin(radians));
            for(auto& vertex:quad){const auto px=vertex.x-item.pivot_x,py=vertex.y-item.pivot_y;
                vertex.x=item.pivot_x+px*c-py*s;vertex.y=item.pivot_y+px*s+py*c;}}
        vertices.insert(vertices.end(),quad.begin(),quad.end());
    }
    flush();
}
void SdlScene::background(const script::BackgroundFrame& frame,std::span<const image::Sprite> environment){
    // Retain one fixed-size render target across transitions, but release the
    // source image immediately so fast-forward cannot keep old scenes alive.
    if(!frame.mask)mask_source_.reset();
    std::erase_if(uploads_,[](const auto& item){return item.image.expired();});
    canvas_.clip({});canvas_.clear({0,0,0,255});
    if(frame.mask && frame.current){
        const auto& layer=*frame.current;const auto& mask=*frame.mask;
        if(!mask.image || !layer.image)throw std::runtime_error("missing native background mask image");
        if(!mask_source_ || mask_source_->image!=layer.image || mask_source_->x!=layer.x || mask_source_->y!=layer.y ||
            mask_source_->width!=layer.width || mask_source_->height!=layer.height || mask_source_->colour!=layer.colour){
            mask_source_=layer;if(!mask_target_)mask_target_=canvas_.target(1280,720);
            canvas_.render_to(mask_target_,[&]{canvas_.clear({0,0,0,255});auto opaque=layer;opaque.opacity=255;draw(opaque);});
        }
        if(frame.previous)draw(*frame.previous);
        sprites(environment);
        canvas_.masked(mask_target_,texture(mask.image),*mask.image,mask.bias,mask.gain,mask.flip_x,mask.flip_y,mask.invert);
    }else{
        if(frame.previous){draw(*frame.previous);sprites(environment);}
        if(frame.current)draw(*frame.current);
        if(!frame.previous)sprites(environment);
    }
}
}
