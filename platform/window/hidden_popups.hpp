#pragma once
#include "host.hpp"
#include <map>
#include <stdexcept>
namespace wa2::window {
// Hidden owned popup windows carry source callbacks and menus. They do not
// need a second native render surface. Showing one requires a visual adapter;
// it must never be silently treated as a visible window.
class HiddenPopups {
    std::map<std::uint32_t,WindowSpec> windows_;
public:
    void create(const WindowSpec& value,std::uint32_t main){
        if(!value.token || value.token==main || windows_.contains(value.token) || windows_.size()>=128 ||
           !(value.style&0x80000000u) || (value.style&0x50000000u) ||
           (value.parent && value.parent!=main) || value.bounds.width<0 || value.bounds.height<0)
            throw std::runtime_error("unsupported auxiliary window specification");
        windows_.emplace(value.token,value);
    }
    bool contains(std::uint32_t id)const{return windows_.contains(id);}
    WindowSpec& get(std::uint32_t id){return windows_.at(id);}
    const WindowSpec& get(std::uint32_t id)const{return windows_.at(id);}
    void show(std::uint32_t id,bool visible){get(id);if(visible)throw std::runtime_error("visible auxiliary popup needs a native visual adapter");}
    void position(std::uint32_t id,const Bounds& bounds,std::uint32_t flags){
        get(id);if(flags&0x40)throw std::runtime_error("visible auxiliary popup needs a native visual adapter");
        get(id).bounds=bounds;
    }
    void destroy(std::uint32_t id){if(!windows_.erase(id))throw std::runtime_error("unknown hidden popup");}
    void clear(){windows_.clear();}
    std::size_t size()const{return windows_.size();}
};
}
