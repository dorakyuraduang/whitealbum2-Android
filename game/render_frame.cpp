#include "render_frame.hpp"
#include "application.hpp"
#include <algorithm>
#include <array>

namespace wa2::game {
void RenderFrame::clear(){background_={};environment_.clear();overlays_.clear();dialogue_={};movie_.reset();movie_active_=false;}
void RenderFrame::compose(const script::BackgroundFrame& background,std::span<const image::Sprite> environment,
        std::span<const std::span<const image::Sprite>> overlays,const image::SceneTransform& dialogue,
        bool active,std::optional<movie::VideoFrame> movie){
    if(movie && !active)throw std::invalid_argument("Native video frame requires active movie presentation");
    background_=active?script::BackgroundFrame{}:background;
    environment_.clear();if(!active)environment_.insert(environment_.end(),environment.begin(),environment.end());
    dialogue_=dialogue;movie_active_=active;movie_=std::move(movie);overlays_.clear();
    std::size_t count{};for(auto items:overlays)count+=items.size();overlays_.reserve(count);
    for(auto items:overlays)overlays_.insert(overlays_.end(),items.begin(),items.end());
    // INAS pieces, glyph shadow/foreground and additive choice passes can
    // share the same source slot. Preserve their original order on equal keys.
    std::stable_sort(overlays_.begin(),overlays_.end(),[](const auto& a,const auto& b){return a.layer==b.layer?a.order<b.order:a.layer<b.layer;});
}
void RenderFrame::capture(Scene& scene){
    const auto active=scene.movie_active();
    if(active){const std::array groups{scene.captions()};compose({}, {},groups,{},true,scene.movie());}
    else{const std::array groups{scene.foreground(),scene.dialogue(),scene.selection(),scene.calendar(),scene.captions()};
        compose(scene.background(),scene.environment(),groups,scene.dialogue_transform());}
}
void RenderFrame::capture(const TitleScene& title){
    const std::array groups{title.sprites()};compose({}, {},groups,{},title.stage()!=TitleStage::view,title.movie());
}
void RenderFrame::capture(Application& application){
    switch(application.page()){
    case Page::story:if(!application.scene())throw std::logic_error("Native story presentation has no scene");capture(*application.scene());return;
    case Page::title:if(!application.title_scene())throw script::Unsupported("Native title presentation has not been prepared");capture(*application.title_scene());return;
    case Page::closed:clear();return;
    case Page::options:case Page::save:case Page::load:case Page::backlog:case Page::extras:
        throw script::Unsupported("Native page view is not implemented: "+std::to_string(unsigned(application.page())));
    }
    throw script::Unsupported("Unknown native presentation page");
}
}
