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
    CHECK(argc==2);const auto image=wa2::aot::initial_image();unsigned dialogs{},menus{},controls{},items{},truncated{},icons{},icon_rejections{};std::map<std::u16string,unsigned> named_types;std::map<unsigned,unsigned> ordinal_types;
    const auto reject_icon=[&](auto operation){bool rejected=false;try{operation();}catch(const std::exception&){rejected=true;}CHECK(rejected);++icon_rejections;};
    for(const auto& resource:wr::resource_directory(image)){
        const auto* type=std::get_if<std::uint16_t>(&resource.type);if(!type || (*type!=4 && *type!=5))continue;const auto bytes=wr::resource_bytes(image,resource);
        if(*type==5){const auto dialog=w::parse_dialog(bytes);++dialogs;controls+=unsigned(dialog.controls.size());CHECK(dialog.bounds[2]>0 && dialog.bounds[3]>0);
            for(const auto& control:dialog.controls){if(const auto* type=std::get_if<std::uint16_t>(&control.type)){++ordinal_types[*type];if(*type==130 && (control.style&31)==3){
                    const auto icon=w::load_icon_resource(image,control.title);CHECK(icon.width==32 && icon.height==32 && icon.dib.size()>4096);++icons;
                    for(std::size_t n=0;n<icon.dib.size();++n)reject_icon([&]{w::parse_icon_bitmap(std::span(icon.dib).first(n),icon.width,icon.height);});
                    for(unsigned offset:{0u,4u,8u,12u,14u,16u,32u}){auto changed=icon.dib;changed[offset]^=1;reject_icon([&]{w::parse_icon_bitmap(changed,icon.width,icon.height);});}
                }}else if(const auto* type=std::get_if<std::u16string>(&control.type))++named_types[*type];else CHECK(false);}}
        else{const auto menu=w::parse_menu(bytes);CHECK(!menu.items.empty());++menus;items+=menu_count(menu.items);}
        // Every truncated prefix of a structural resource must be rejected,
        // including the final string terminator and child item records.
        for(std::size_t size=0;size<bytes.size();++size){bool rejected=false;try{if(*type==5)w::parse_dialog(bytes.first(size));else w::parse_menu(bytes.first(size));}catch(const std::exception&){rejected=true;}if(rejected)++truncated;}
    }
    unsigned group_checks{};for(const auto& resource:wr::resource_directory(image)){const auto* type=std::get_if<std::uint16_t>(&resource.type);if(!type || *type!=14)continue;
        const auto data=wr::resource_bytes(image,resource);for(unsigned desired:{16u,24u,32u,48u,128u,256u}){const auto entry=w::select_icon(data,desired);CHECK(entry.width==desired && entry.height==desired);++group_checks;}
        for(std::size_t n=0;n<data.size();++n)reject_icon([&]{w::select_icon(data.first(n));});
        for(unsigned offset:{0u,2u,4u,9u,14u,18u}){auto changed=std::vector<std::uint8_t>(data.begin(),data.end());changed[offset]=0;changed[offset+1]=0;if(offset==0 || offset==9)changed[offset]=1;reject_icon([&]{w::select_icon(changed);});}}
    reject_icon([&]{w::parse_icon_bitmap({},0,32);});reject_icon([&]{w::parse_icon_bitmap({},32,257);});reject_icon([&]{w::load_icon_resource(image,std::uint16_t(65534));});
    CHECK(dialogs==7 && menus==2 && controls>20 && items>10 && truncated>100 && icons==2 && group_checks==6);
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.shared-window-resources.v1\",\"dialogs\":"<<dialogs<<",\"controls\":"<<controls<<",\"menus\":"<<menus<<",\"menu_items\":"<<items<<",\"rejected_truncated_prefixes\":"<<truncated<<",\"cached_icon_controls\":"<<icons<<",\"icon_size_selections\":"<<group_checks<<",\"rejected_icon_inputs\":"<<icon_rejections<<",\"control_classes\":{";
    bool first=true;for(auto [type,count]:ordinal_types){if(!first)out<<',';first=false;out<<wr::quote("#"+std::to_string(type))<<':'<<count;}
    for(const auto& [type,count]:named_types){if(!first)out<<',';first=false;std::string ascii;for(auto c:type){CHECK(c<128);ascii+=char(c);}out<<wr::quote(ascii)<<':'<<count;}
    out<<"},\"source_executable_opened\":false,\"window_created\":false,\"screen_access\":false,\"game_launched\":false,\"passed\":true}\n";
    std::cout<<"PASS shared window resources: "<<dialogs<<" cached dialog templates, "<<controls<<" controls and "<<items<<" menu items, without native windows.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
