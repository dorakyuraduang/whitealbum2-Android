#pragma once
#include "resource_templates.hpp"
namespace wa2::window {
enum class MenuUpdate {none,state,layout,structure};
inline MenuUpdate menu_update(const std::vector<MenuItem>& before,const std::vector<MenuItem>& after){
    if(before.size()!=after.size())return MenuUpdate::structure;
    auto result=MenuUpdate::none;
    for(unsigned n=0;n<before.size();++n){const auto& old=before[n];const auto& value=after[n];
        if(old.state!=value.state && result==MenuUpdate::none)result=MenuUpdate::state;
        if(old.id!=value.id || old.type!=value.type || old.help!=value.help || old.text!=value.text)result=MenuUpdate::layout;
        const auto children=menu_update(old.children,value.children);
        if(children==MenuUpdate::structure)return children;
        if(unsigned(children)>unsigned(result))result=children;
    }return result;
}
}
