#pragma once
#include "resource_catalog.hpp"
#include "stream_voice.hpp"
#include "system_sounds.hpp"

namespace wa2::audio {
// Original SE/BGM/speech lookup shared by stories and all native menus.
// Missing members fall back; permission, archive and I/O failures propagate.
SoundResource effect_resource(resource::Catalog&,std::int32_t id);
OggPlaylist music_resources(resource::Catalog&,std::int32_t id,bool loop);
std::shared_ptr<resource::Source> speech_resource(resource::Catalog&,std::int32_t chapter,std::int32_t line,std::int32_t speaker);
}
