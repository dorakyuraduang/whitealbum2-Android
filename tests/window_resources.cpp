#include "process.hpp"
#include "../platform/window/resource_templates.hpp"
#include <winrecomp/resources.hpp>
#include <fstream>
#include <iostream>
#include <map>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
namespace w=wa2::window;
unsigned menu_count(const std::vector<w::MenuItem>& items){unsigned count{};for(const auto& item:items)count+=1+menu_count(item.children);return count;}
int main(int argc,char** argv){try{
    CHECK(argc==2);const auto image=wa2::aot::initial_image();unsigned dialogs{},menus{},controls{},items{},truncated{};std::map<std::u16string,unsigned> named_types;std::map<unsigned,unsigned> ordinal_types;
    for(const auto& resource:wr::resource_directory(image)){
        const auto* type=std::get_if<std::uint16_t>(&resource.type);if(!type || (*type!=4 && *type!=5))continue;const auto bytes=wr::resource_bytes(image,resource);
        if(*type==5){const auto dialog=w::parse_dialog(bytes);++dialogs;controls+=unsigned(dialog.controls.size());CHECK(dialog.bounds[2]>0 && dialog.bounds[3]>0);
            for(const auto& control:dialog.controls){if(const auto* type=std::get_if<std::uint16_t>(&control.type))++ordinal_types[*type];else if(const auto* type=std::get_if<std::u16string>(&control.type))++named_types[*type];else CHECK(false);}}
        else{const auto menu=w::parse_menu(bytes);CHECK(!menu.items.empty());++menus;items+=menu_count(menu.items);}
        // Every truncated prefix of a structural resource must be rejected,
        // including the final string terminator and child item records.
        for(std::size_t size=0;size<bytes.size();++size){bool rejected=false;try{if(*type==5)w::parse_dialog(bytes.first(size));else w::parse_menu(bytes.first(size));}catch(const std::exception&){rejected=true;}if(rejected)++truncated;}
    }
    CHECK(dialogs==7 && menus==2 && controls>20 && items>10 && truncated>100);
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.shared-window-resources.v1\",\"dialogs\":"<<dialogs<<",\"controls\":"<<controls<<",\"menus\":"<<menus<<",\"menu_items\":"<<items<<",\"rejected_truncated_prefixes\":"<<truncated<<",\"control_classes\":{";
    bool first=true;for(auto [type,count]:ordinal_types){if(!first)out<<',';first=false;out<<wr::quote("#"+std::to_string(type))<<':'<<count;}
    for(const auto& [type,count]:named_types){if(!first)out<<',';first=false;std::string ascii;for(auto c:type){CHECK(c<128);ascii+=char(c);}out<<wr::quote(ascii)<<':'<<count;}
    out<<"},\"source_executable_opened\":false,\"window_created\":false,\"screen_access\":false,\"game_launched\":false,\"passed\":true}\n";
    std::cout<<"PASS shared window resources: "<<dialogs<<" cached dialog templates, "<<controls<<" controls and "<<items<<" menu items, without native windows.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
