#pragma once
#include "support.hpp"
#include <cctype>
namespace wr::port {
inline std::string selected_path(Process& p,const std::filesystem::path& path,bool directory){
    const auto canonical=std::filesystem::weakly_canonical(path);auto key=canonical.generic_string();
    for(auto& ch:key)ch=char(std::tolower(static_cast<unsigned char>(ch)));
    p.state().selected_paths[key]={canonical,directory};return canonical.string();
}
}
