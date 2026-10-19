// Produce native widget transport JSON in RAM, without JNI, widgets or a game.
#include "process.hpp"
#include "../platform/window/widget_json.hpp"
#include "../platform/compat/codepage.hpp"
#include <winrecomp/resources.hpp>
#include <fstream>
#include <iostream>
#include <random>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
namespace w=wa2::window;
void units(std::ostream& out,std::u16string_view value){out<<'[';bool first=true;for(auto c:value){if(!first)out<<',';first=false;out<<unsigned(c);}out<<']';}
void expected_menu(std::ostream& out,const std::vector<w::MenuItem>& items){
    out<<'[';bool first=true;for(const auto& item:items){if(!first)out<<',';first=false;out<<"{\"id\":"<<item.id<<",\"type\":"<<item.type<<",\"state\":"<<item.state<<",\"text\":";units(out,item.text);out<<",\"children\":";expected_menu(out,item.children);out<<'}';}out<<']';
}
void expected_dialog(std::ostream& out,const w::DialogState& d){
    out<<"{\"token\":"<<d.token<<",\"owner\":"<<d.owner<<",\"title\":";units(out,d.definition.title);out<<",\"visible\":"<<(d.visible?"true":"false")<<",\"width\":"<<d.definition.bounds[2]<<",\"height\":"<<d.definition.bounds[3]<<",\"controls\":[";
    bool first=true;for(const auto& c:d.controls){if(!first)out<<',';first=false;std::u16string type;
        if(const auto* ordinal=std::get_if<std::uint16_t>(&c.definition.type)){const auto ascii="#"+std::to_string(*ordinal);type.assign(ascii.begin(),ascii.end());}
        else if(const auto* name=std::get_if<std::u16string>(&c.definition.type))type=*name;
        out<<"{\"id\":"<<c.definition.id<<",\"token\":"<<c.token<<",\"type\":";units(out,type);out<<",\"text\":";units(out,c.text);out<<",\"style\":"<<c.definition.style<<",\"bounds\":[";
        for(unsigned n=0;n<4;++n){if(n)out<<',';out<<c.definition.bounds[n];}
        out<<"],\"check\":"<<c.check<<",\"position\":"<<c.position<<",\"minimum\":"<<c.minimum<<",\"maximum\":"<<c.maximum<<",\"selection\":"<<c.selection<<",\"enabled\":"<<(c.enabled?"true":"false")<<",\"visible\":"<<(c.visible?"true":"false")<<",\"items\":[";
        bool first_item=true;for(const auto& item:c.items){if(!first_item)out<<',';first_item=false;units(out,item);}out<<"]}";
    }out<<"]}";
}
int main(int argc,char** argv){try{
    CHECK(argc==2);CHECK(w::json::text(u"游戏设置")=="\"\\u6e38\\u620f\\u8bbe\\u7f6e\"");
    std::vector<std::u16string> samples{u"",u"Save && Load\tF5",u"游戏设置(&S)",u"音量／文字速度",u"はじめから／オプション",u"Français—é",u"☰ ✓ \U0001f4be \U0001f3ae",u"D:\\Wa2Res\\中文目录\\存档.sav",u"引号\"、反斜杠\\和换行\n\t\r"};
    samples.push_back(std::u16string{0,1,8,9,10,12,13,31,127,128});
    samples.push_back(std::u16string{char16_t(0xd800),char16_t(0xdc00),char16_t(0xdfff),u'x',char16_t(0xdbff)});
    std::u16string all;for(unsigned n=0;n<65536;++n)all+=char16_t(n);samples.push_back(std::move(all));
    std::mt19937 random(0x554e4943);for(unsigned n=0;n<64;++n){std::u16string value;for(unsigned i=0;i<n*3;++i)value+=char16_t(random());samples.push_back(std::move(value));}
    const auto encoded=wa2::platform::compat::encode_text(936,u"设置：保存、读取").bytes;samples.push_back(wa2::platform::compat::decode_text(936,encoded,true));
    std::ofstream out(argv[1],std::ios::binary);out<<"{\"strings\":[";
    for(unsigned n=0;n<samples.size();++n){if(n)out<<',';out<<"{\"units\":";units(out,samples[n]);out<<",\"value\":"<<w::json::text(samples[n])<<'}';}
    // Retain an executable reproduction of the former byte-quoting error.
    out<<"],\"before\":[";for(unsigned n:{2u,4u,6u}){if(n!=2)out<<',';const auto utf8=wa2::platform::compat::encode_text(65001,samples[n]).bytes;
        out<<"{\"units\":";units(out,samples[n]);out<<",\"value\":"<<wr::quote(std::string(utf8.begin(),utf8.end()))<<'}';}
    out<<"],\"menus\":[";const auto image=wa2::aot::initial_image();unsigned menus{},dialogs{};
    for(const auto& resource:wr::resource_directory(image)){const auto* type=std::get_if<std::uint16_t>(&resource.type);if(!type || *type!=4)continue;
        const auto model=w::parse_menu(wr::resource_bytes(image,resource));if(menus++)out<<',';out<<"{\"expected\":";expected_menu(out,model.items);out<<",\"transport\":"<<w::json::menu(model.items)<<'}';}
    w::MenuItem child;child.id=0xffff1234;child.type=0;child.state=8;child.text=samples[6];w::MenuItem root;root.text=samples[2];root.children.push_back(child);
    w::MenuItem separator;separator.type=0x800;const std::vector<w::MenuItem> synthetic{root,separator};out<<",{\"expected\":";expected_menu(out,synthetic);out<<",\"transport\":"<<w::json::menu(synthetic)<<'}';
    out<<"],\"dialogs\":[";
    for(const auto& resource:wr::resource_directory(image)){const auto* type=std::get_if<std::uint16_t>(&resource.type);if(!type || *type!=5)continue;
        w::DialogState state;state.definition=w::parse_dialog(wr::resource_bytes(image,resource));state.token=0xb1000000+dialogs*16;state.owner=0xb2000010;state.visible=dialogs%2==0;
        for(const auto& control:state.definition.controls){w::ControlState value;value.definition=control;value.token=0xb3000000+unsigned(state.controls.size())*16;if(const auto* text=std::get_if<std::u16string>(&control.title))value.text=*text;
            value.items={samples[2],samples[4],samples[6]};value.minimum=-17;value.position=13;value.maximum=200;value.selection=-1;value.check=2;value.enabled=state.controls.size()%2==0;value.visible=state.controls.size()%3!=0;state.controls.push_back(std::move(value));}
        if(dialogs++)out<<',';out<<"{\"expected\":";expected_dialog(out,state);out<<",\"transport\":"<<w::json::dialog(state)<<'}';
    }
    CHECK(menus==2 && dialogs==7);out<<"],\"cached_menus\":"<<menus<<",\"cached_dialogs\":"<<dialogs<<",\"game_launched\":false,\"screen_access\":false,\"source_executable_opened\":false}\n";CHECK(out.good());
    std::cout<<"PASS native widget JSON fixture: "<<samples.size()<<" Unicode inputs, "<<menus<<" cached menus and "<<dialogs<<" dialogs; no native UI.\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
