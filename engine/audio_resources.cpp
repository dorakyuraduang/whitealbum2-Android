#include "audio_resources.hpp"
#include <cstdio>

namespace wa2::audio {
SoundResource effect_resource(resource::Catalog& resources,std::int32_t id){
    std::array<char,48> name{};
    for(bool introductory:{false,true})if(const auto* bank=resources.optional("se",introductory)){
        std::snprintf(name.data(),name.size(),"SE_%04d.ogg",int(id));auto index=bank->find_first(name.data());
        if(index!=resource::Archive::missing)return {bank->open_member(index),true};
        std::snprintf(name.data(),name.size(),"SE_%04d.wav",int(id));index=bank->find_first(name.data());
        if(index!=resource::Archive::missing)return {bank->open_member(index),false};
    }
    throw resource::NotFound("missing original sound effect "+std::to_string(id));
}
OggPlaylist music_resources(resource::Catalog& resources,std::int32_t id,bool loop){
    if(id<0 || id>=200)throw resource::Error("invalid original music resource number");
    std::array<char,32> name{};
    for(bool introductory:{false,true})if(const auto* bank=resources.optional("bgm",introductory)){
        std::snprintf(name.data(),name.size(),"BGM_%03d.ogg",int(id));const auto full=bank->find_first(name.data());
        std::snprintf(name.data(),name.size(),"BGM_%03d_a.ogg",int(id));
        if(full!=resource::Archive::missing || bank->find_first(name.data())!=resource::Archive::missing)return music_playlist(*bank,id,loop);
    }
    throw resource::NotFound("missing original background music "+std::to_string(id));
}
std::shared_ptr<resource::Source> speech_resource(resource::Catalog& resources,std::int32_t chapter,std::int32_t line,std::int32_t speaker){
    std::array<char,64> name{};std::snprintf(name.data(),name.size(),"%04d_%04d_%02d.ogg",int(chapter),int(line),int(speaker));
    for(bool introductory:{false,true})if(const auto* bank=resources.optional("voice",introductory)){
        const auto index=bank->find_first(name.data());if(index!=resource::Archive::missing)return bank->open_member(index);
    }
    throw resource::NotFound("missing original speech "+std::string(name.data()));
}
}
