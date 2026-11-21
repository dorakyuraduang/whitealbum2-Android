#include "special_subtitle_files.hpp"
#include "../engine/special_subtitles.hpp"
#include <fstream>
#include <stdexcept>

namespace wa2::platform {
std::shared_ptr<const subtitles::Catalogue> special_subtitle_files(const std::filesystem::path& root){
    const auto read=[&](const char* name,std::size_t maximum){const auto path=root/name;std::ifstream input(path,std::ios::binary|std::ios::ate);if(!input)throw std::runtime_error("SC subtitle resource missing: "+std::string(name));const auto size=input.tellg();if(size<=0 || std::uint64_t(size)>maximum)throw std::runtime_error("SC subtitle resource exceeds its bound");std::vector<std::uint8_t> data(std::size_t(size),0);input.seekg(0);if(!input.read(reinterpret_cast<char*>(data.data()),size))throw std::runtime_error("Short SC subtitle resource read");return data;};
    return subtitles::compile_special_binary(read("subtext.dat",1024*1024),read("subfont.tga",8*1024*1024),read("songfont.tga",8*1024*1024));
}
}
