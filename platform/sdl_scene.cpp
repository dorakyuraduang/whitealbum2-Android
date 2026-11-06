#include "sdl_scene.hpp"
#include "scene_geometry.hpp"
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
    const CanvasColour colour{std::uint8_t(std::clamp(layer.colour[0]*2,0,255)),std::uint8_t(std::clamp(layer.colour[1]*2,0,255)),
        std::uint8_t(std::clamp(layer.colour[2]*2,0,255)),layer.opacity};
    const image::QuadGeometry geometry{layer.x,layer.y,layer.width,layer.height,layer.source_x,layer.source_y,
        layer.source_width?layer.source_width:float(layer.image->width),layer.source_height?layer.source_height:float(layer.image->height),
        layer.angle,layer.pivot_x,layer.pivot_y,layer.scale_x,layer.scale_y};
    const auto quad=image::scene_quad(geometry,layer.image->width,layer.image->height);if(!quad)return;
    std::array<CanvasVertex,6> vertices;for(unsigned n=0;n<vertices.size();++n){const auto& v=(*quad)[n];vertices[n]={v.x,v.y,v.u,v.v,colour};}
    canvas_.triangles(texture(layer.image),vertices);
    if(std::any_of(layer.colour.begin(),layer.colour.end(),[](auto channel){return channel>128;})){
        const CanvasColour light{std::uint8_t(std::clamp(layer.colour[0]*2-256,0,255)),std::uint8_t(std::clamp(layer.colour[1]*2-256,0,255)),
            std::uint8_t(std::clamp(layer.colour[2]*2-256,0,255)),layer.opacity};
        auto additive=vertices;for(auto& vertex:additive)vertex.colour=light;canvas_.triangles({},additive,CanvasBlend::add);
    }
}
void SdlScene::sprites(std::span<const image::Sprite> items,const image::SceneTransform& transform){
    if(items.size()>65536)throw std::runtime_error("native sprite batch exceeds limit");
    std::erase_if(uploads_,[](const auto& item){return item.image.expired();});
    auto& vertices=sprite_vertices_;vertices.clear();std::shared_ptr<const image::Raster> source;auto blend=image::SpriteBlend::alpha;
    if(vertices.capacity()<items.size()*6)vertices.reserve(items.size()*6);
    const auto flush=[&]{if(!vertices.empty()){canvas_.triangles(texture(source),vertices,blend==image::SpriteBlend::add?CanvasBlend::add:CanvasBlend::alpha);vertices.clear();}};
    for(const auto& item:items){
        if(!item.image || !item.alpha)continue;
        const auto quad=image::scene_quad({item.x,item.y,item.width,item.height,item.source_x,item.source_y,item.source_width,item.source_height,
            item.angle,item.pivot_x,item.pivot_y,1,1,item.flip,transform},item.image->width,item.image->height);if(!quad)continue;
        if(source!=item.image || blend!=item.blend){flush();source=item.image;blend=item.blend;}
        const CanvasColour colour{item.red,item.green,item.blue,item.alpha};
        for(const auto& v:*quad)vertices.push_back({v.x,v.y,v.u,v.v,colour});
    }
    flush();
}
void SdlScene::background(const script::BackgroundFrame& frame,std::span<const image::Sprite> environment){
    if(frame.transform.identity()){background_contents(frame,environment);return;}
    if(!world_target_)world_target_=canvas_.target(1280,720);
    canvas_.render_to(world_target_,[&]{background_contents(frame,environment);});
    canvas_.clip({});canvas_.clear({0,0,0,255});
    const auto quad=image::scene_quad({0,0,1280,720,0,0,1280,720,0,0,0,1,1,0,frame.transform},1280,720);
    if(quad){std::array<CanvasVertex,6> vertices;for(unsigned n=0;n<vertices.size();++n){const auto& v=(*quad)[n];vertices[n]={v.x,v.y,v.u,v.v,{255,255,255,255}};}canvas_.triangles(world_target_,vertices);}
}
void SdlScene::background_contents(const script::BackgroundFrame& frame,std::span<const image::Sprite> environment){
    // Retain one fixed-size render target across transitions, but release the
    // source image immediately so fast-forward cannot keep old scenes alive.
    if(!frame.mask)mask_source_.reset();
    std::erase_if(uploads_,[](const auto& item){return item.image.expired();});
    canvas_.clip({});canvas_.clear({0,0,0,255});
    if(frame.mask && frame.current){
        const auto& layer=*frame.current;const auto& mask=*frame.mask;
        if(!mask.image || !layer.image)throw std::runtime_error("missing native background mask image");
        if(!mask_source_ || mask_source_->image!=layer.image || mask_source_->x!=layer.x || mask_source_->y!=layer.y ||
            mask_source_->width!=layer.width || mask_source_->height!=layer.height || mask_source_->colour!=layer.colour ||
            mask_source_->angle!=layer.angle || mask_source_->pivot_x!=layer.pivot_x || mask_source_->pivot_y!=layer.pivot_y ||
            mask_source_->source_x!=layer.source_x || mask_source_->source_y!=layer.source_y || mask_source_->source_width!=layer.source_width ||
            mask_source_->source_height!=layer.source_height || mask_source_->scale_x!=layer.scale_x || mask_source_->scale_y!=layer.scale_y){
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
