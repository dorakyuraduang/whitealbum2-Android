#include "sdl_presentation.hpp"

namespace wa2::platform {
void SdlPresentation::draw(const game::RenderFrame& frame){
    if(frame.movie_active()){
        if(!movie_active_)scene_.reset_textures();
        if(frame.movie())movie_.draw(*frame.movie());
        else{canvas_.clip({});canvas_.clear({0,0,0,255});movie_.reset();}
    }else{
        movie_.reset();scene_.background(frame.background(),frame.environment());
    }
    movie_active_=frame.movie_active();const auto items=frame.overlays();
    const auto transform=[&](const auto& item){return item.space==image::SpriteSpace::dialogue?frame.dialogue_transform():image::SceneTransform{};};
    for(std::size_t first=0;first<items.size();){
        const auto outer=transform(items[first]);auto end=first+1;
        while(end<items.size() && transform(items[end])==outer)++end;
        // Only adjacent equal transforms may batch. Layer order takes
        // precedence over reducing draw calls, including independent bitmaps.
        scene_.sprites(items.subspan(first,end-first),outer);first=end;
    }
    // This also releases expired uploads after an empty title/movie frame.
    if(items.empty())scene_.sprites({});
}
}
