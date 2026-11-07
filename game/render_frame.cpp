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
void RenderFrame::capture(Scene& scene,bool dialogue){
    const auto active=scene.movie_active();
    if(active){const std::array groups{scene.captions()};compose({}, {},groups,{},true,scene.movie());}
    else{const std::array groups{scene.foreground(),dialogue?scene.dialogue():std::span<const image::Sprite>{},dialogue?scene.selection():std::span<const image::Sprite>{},scene.calendar(),scene.captions()};
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
    case Page::options:case Page::save:case Page::load:{
        const bool options=application.page()==Page::options;
        if(options?!application.options_view():!application.save_view() && !application.quick_view())throw script::Unsupported("Native modal presentation has not been prepared");
        if(application.navigation().size()!=2)throw script::Unsupported("Native options presentation over a modal page is not implemented");
        if(application.scene())capture(*application.scene());else if(application.title_scene())capture(*application.title_scene());else throw script::Unsupported("Native options presentation has no parent scene");
        const auto sprites=options?application.options_view()->sprites():application.quick_view()?application.quick_view()->sprites():application.save_view()->sprites();
        overlays_.insert(overlays_.end(),sprites.begin(),sprites.end());
        std::stable_sort(overlays_.begin(),overlays_.end(),[](const auto& a,const auto& b){return a.layer==b.layer?a.order<b.order:a.layer<b.layer;});return;
    }
    case Page::backlog:
        if(!application.backlog_view() || !application.scene() || application.navigation().size()!=2)throw script::Unsupported("Native backlog presentation has not been prepared");
        capture(*application.scene(),false);overlays_.insert(overlays_.end(),application.backlog_view()->sprites().begin(),application.backlog_view()->sprites().end());
        std::stable_sort(overlays_.begin(),overlays_.end(),[](const auto& a,const auto& b){return a.layer==b.layer?a.order<b.order:a.layer<b.layer;});return;
    case Page::extras:
        if((!application.music_view() && !application.extra_grid() && !application.commentary_view()) || !application.title_scene() || application.navigation().size()!=2)throw script::Unsupported("Native extras presentation has not been prepared");
        capture(*application.title_scene());{const auto sprites=application.music_view()?application.music_view()->sprites():application.extra_grid()?application.extra_grid()->sprites():application.commentary_view()->sprites();overlays_.insert(overlays_.end(),sprites.begin(),sprites.end());}
        std::stable_sort(overlays_.begin(),overlays_.end(),[](const auto& a,const auto& b){return a.layer==b.layer?a.order<b.order:a.layer<b.layer;});return;
    }
    throw script::Unsupported("Unknown native presentation page");
}
}
