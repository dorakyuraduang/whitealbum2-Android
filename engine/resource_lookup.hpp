#pragma once
#include "resource_archive.hpp"

namespace wa2::resource {
// Query an initialized source directory without reopening its backing archive.
// Retain its midpoint and endpoint choices, including duplicate-name results.
// NameAt supplies fresh owned bytes; no pointer, handle or index is cached.
template<class NameAt>
std::size_t find_directory_record(std::size_t count,std::string_view name,NameAt name_at){
    std::size_t low=0,high=count;
    while(low<high){
        const auto mid=low+(high-low)/2;
        const auto order=compare_names(name_at(mid),name);
        if(!order)return mid;
        if(order<0){if(low==mid)break;low=mid;}
        else high=mid;
    }
    return Archive::missing;
}
}
