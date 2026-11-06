#include "sdl_scene.hpp"
#include "scene_geometry.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace wa2::platform {
namespace {
CanvasBlend canvas_blend(image::SpriteBlend blend){switch(blend){case image::SpriteBlend::alpha:return CanvasBlend::alpha;case image::SpriteBlend::add:return CanvasBlend::add;
    case image::SpriteBlend::screen:return CanvasBlend::screen;case image::SpriteBlend::multiply:return CanvasBlend::modulate;}throw std::runtime_error("Unknown native sprite blend");}
}
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
    sprites_pass(items,transform,{});
}
void SdlScene::sprites_pass(std::span<const image::Sprite> items,const image::SceneTransform& transform,std::optional<bool> previous){
    if(items.size()>65536)throw std::runtime_error("native sprite batch exceeds limit");
    std::erase_if(uploads_,[](const auto& item){return item.image.expired();});
    auto& vertices=sprite_vertices_;vertices.clear();std::shared_ptr<const image::Raster> source;auto blend=image::SpriteBlend::alpha;
    if(vertices.capacity()<items.size()*6)vertices.reserve(items.size()*6);
    const auto flush=[&]{if(!vertices.empty()){canvas_.triangles(texture(source),vertices,canvas_blend(blend));vertices.clear();}};
    for(const auto& item:items){
        if(previous && !image::in_world(item.world,*previous))continue;
        if(!item.image || !image::contributes(item.blend,item.alpha))continue;
        const auto quad=image::scene_quad({item.x,item.y,item.width,item.height,item.source_x,item.source_y,item.source_width,item.source_height,
            item.angle,item.pivot_x,item.pivot_y,1,1,item.flip,transform},item.image->width,item.image->height);if(!quad)continue;
        if(source!=item.image || blend!=item.blend){flush();source=item.image;blend=item.blend;}
        const CanvasColour colour{item.red,item.green,item.blue,item.alpha};
        for(const auto& v:*quad)vertices.push_back({v.x,v.y,v.u,v.v,colour});
    }
    flush();
}
void SdlScene::background(const script::BackgroundFrame& frame,std::span<const image::Sprite> environment){
    prepare_worlds(frame,environment);
    if(frame.transform.identity()){background_contents(frame,environment);return;}
    if(!world_target_)world_target_=canvas_.target(1280,720);
    canvas_.render_to(world_target_,[&]{background_contents(frame,environment);});
    canvas_.clip({});canvas_.clear({0,0,0,255});
    draw_target(world_target_,frame.transform,255);
}
void SdlScene::draw_target(const std::shared_ptr<CanvasTexture>& target,const image::SceneTransform& transform,std::uint8_t opacity){
    if(!opacity)return;const auto quad=image::scene_quad({0,0,1280,720,0,0,1280,720,0,0,0,1,1,0,transform},1280,720);
    if(quad){std::array<CanvasVertex,6> vertices;for(unsigned n=0;n<vertices.size();++n){const auto& v=(*quad)[n];vertices[n]={v.x,v.y,v.u,v.v,{255,255,255,opacity}};}canvas_.triangles(target,vertices,opacity==255?CanvasBlend::copy:CanvasBlend::alpha);}
}
void SdlScene::prepare_worlds(const script::BackgroundFrame& frame,std::span<const image::Sprite> environment){
    const bool composite=frame.current && (frame.mask || frame.transition || (frame.previous && frame.current->opacity<255));
    if(!composite){mask_source_.reset();previous_source_.reset();}
    std::erase_if(uploads_,[](const auto& item){return item.image.expired();});
    if(!composite)return;
    // Both bounded world targets include their own characters/particles. A
    // preserved character therefore participates in both sides of a fade.
    // Constant empty environments reuse their image upload and target; live
    // animation invalidates only its participating bank, never the old PAKs.
    const auto render_world=[&](bool previous){
        auto& target=previous?previous_target_:mask_target_;auto& cache=previous?previous_source_:mask_source_;
        auto layer=previous?frame.previous:frame.current;if(layer)layer->opacity=255;
        const bool dynamic=std::any_of(environment.begin(),environment.end(),[&](const auto& item){return item.image && image::contributes(item.blend,item.alpha) && image::in_world(item.world,previous);});
        if(!target)target=canvas_.target(1280,720);
        if(dynamic || !layer || !cache || *cache!=*layer){canvas_.render_to(target,[&]{canvas_.clip({});canvas_.clear({0,0,0,255});if(layer)draw(*layer);sprites_pass(environment,{},previous);});
            if(dynamic)cache.reset();else cache=std::move(layer);}
    };
    render_world(true);render_world(false);
}
void SdlScene::background_contents(const script::BackgroundFrame& frame,std::span<const image::Sprite> environment){
    const bool composite=frame.current && (frame.mask || frame.transition || (frame.previous && frame.current->opacity<255));
    canvas_.clip({});canvas_.clear({0,0,0,255});
    if(!frame.current){if(frame.previous)draw(*frame.previous);sprites_pass(environment,{},true);return;}
    if(!composite){draw(*frame.current);sprites_pass(environment,{},false);return;}
    // World passes are already prepared before the optional outer shake pass.
    // Compositing here never recursively enters another render target.
    const auto transition=frame.transition.value_or(script::BackgroundFrame::Transition{{},{{},frame.current->opacity}});
    draw_target(previous_target_,transition.previous.transform,transition.previous.opacity);
    if(frame.mask){const auto& mask=*frame.mask;if(!mask.image)throw std::runtime_error("Missing native background mask image");
        canvas_.masked(mask_target_,texture(mask.image),*mask.image,mask.bias,mask.gain,mask.flip_x,mask.flip_y,mask.invert);
    }else draw_target(mask_target_,transition.current.transform,transition.current.opacity);
}
}
