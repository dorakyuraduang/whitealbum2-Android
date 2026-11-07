// Parse original resource records only. No script is executed, scene created,
// game started, image decoded, device opened or source executable inspected.
#include "script_runtime.hpp"
#include "file_io.hpp"
#include "resource_catalog.hpp"
#include "sprite_animation.hpp"
#include "image_colours.hpp"
#include "native_title_atlases.hpp"
#include "native_options_atlases.hpp"
#include "native_save_atlases.hpp"
#include "scene_calendar.hpp"
#include <bit>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>

// A bounded walk over literal records only. It never starts Runtime/Session,
// reads variables, follows branches or invokes a script command.
std::optional<std::int32_t> integer_literal(const wa2::script::Program& program,std::uint32_t& cursor,unsigned depth=0){
    if(depth>8)return {};std::optional<std::uint32_t> previous;
    for(unsigned bytes=4;bytes<=12 && bytes<=cursor;bytes+=4)if(program.instruction_offset(cursor-bytes)){previous=cursor-bytes;break;}
    if(!previous)return {};cursor=*previous;const auto kind=program.word(cursor),type=program.word(cursor+4);
    if(kind==5 && type>=1 && type<=3)return std::bit_cast<std::int32_t>(program.word(cursor+8));
    if(kind==6 && type==23){if(auto value=integer_literal(program,cursor,depth+1))return std::bit_cast<std::int32_t>(0u-std::uint32_t(*value));}return {};
}

int main(int argc,char** argv){try{
    if(argc!=3)throw std::runtime_error("usage: native_script_catalogue resource-directory report.json");
    const auto path=std::filesystem::path(argv[1])/"ck-gal.pak";
    auto archive=wa2::resource::Archive::open(wa2::platform::open_resource_file(path));wa2::script::Library library(archive);
    unsigned scenes{},labels{},calls{},calendar_calls{},calendar_literals{},calendar_unknown{},calendar_201{};std::set<std::uint32_t> commands;
    for(const auto& member:archive.members()){
        if(!member.name.ends_with(".bnr"))continue;
        const auto name=member.name.substr(0,member.name.size()-4);const auto program=library.load(name);++scenes;
        labels+=unsigned(program->labels().size());
        for(const auto& label:program->labels())if(!program->instruction_offset(label.offset))throw std::runtime_error("Original script label boundary mismatch");
        for(const auto& call:program->commands()){
            library.functions().at(call.index);commands.insert(call.index);++calls;
            if(!program->instruction_offset(call.offset))throw std::runtime_error("Original script command boundary mismatch");
            if(call.index==203){
                ++calendar_calls;auto cursor=call.offset;std::array<std::int32_t,5> args{};bool known=true;
                for(unsigned n=5;n>0;--n){const auto value=integer_literal(*program,cursor);if(!value){known=false;break;}args[n-1]=*value;}
                if(!known){++calendar_unknown;continue;}++calendar_literals;
                const auto date=wa2::script::SceneCalendar::date(args[0],args[1],args[2],args[3]);
                if(args[4] && !wa2::script::SceneCalendar::supports_display(date))throw std::runtime_error("Unsupported literal calendar display in "+name+" at record "+std::to_string(call.offset));
                if(date.year==201)++calendar_201;
            }
        }
    }
    if(!scenes || !calls)throw std::runtime_error("Missing original script catalogue");
    wa2::resource::Catalog resources([&](std::string_view name)->std::shared_ptr<wa2::resource::Source>{
        const auto file=std::filesystem::path(argv[1])/std::filesystem::path(name);
        if(!std::filesystem::exists(file))return {};return wa2::platform::open_resource_file(file);
    });
    unsigned menu_atlases{};
    const auto menu_atlas=[&](const auto& atlas){
        const auto header=wa2::image::inspect_game_tga(resources.data("grp",atlas.name));
        if(header.width<atlas.w || header.height<atlas.h)throw std::runtime_error("Original menu crop exceeds image dimensions: "+std::string(atlas.name)+" expected "+std::to_string(atlas.w)+"x"+std::to_string(atlas.h)+" actual "+std::to_string(header.width)+"x"+std::to_string(header.height));++menu_atlases;
    };
    for(const auto& atlas:wa2::test::title_atlases)menu_atlas(atlas);
    for(const auto& atlas:wa2::test::options_atlases)menu_atlas(atlas);
    for(const auto& atlas:wa2::test::save_atlases)menu_atlas(atlas);
    struct MenuAtlas {const char* name;unsigned w,h;};
    constexpr MenuAtlas extras[]{
        {"sys_04000.tga",1280,40},{"sys_04100.tga",400,128},{"sys_04101.tga",344,1512},{"sys_04021.tga",288,40},{"sys_04030.tga",1024,56},{"sys_04031.tga",512,1512},
        {"sys_05000.tga",1280,36},{"sys_06000.tga",1280,36},{"sys_05100.tga",272,160},{"sys_05110.tga",272,160},{"sys_05020.tga",144,128},{"sys_05201.tga",840,96},{"sys_05021.tga",288,40},{"sys_06021.tga",288,40},{"sys_05900.tga",272,160},{"sys_05901.tga",272,160},{"sys_05902.tga",272,160},{"sys_05200.tga",1000,24},
        {"sys_07000.tga",1280,40},{"sys_07100.tga",1280,720},{"sys_07020.tga",160,120},{"sys_07021.tga",288,40},
        {"sys_07200.tga",1280,720},{"sys_07201.tga",1280,720},{"sys_07202.tga",1280,720},{"sys_07203.tga",1280,720},{"sys_07204.tga",1280,720}
    };
    for(const auto& atlas:extras)menu_atlas(atlas);
    unsigned calendar_atlases{};
    for(unsigned n=0;n<4;++n){
        constexpr std::string_view names[]{"calender_00.tga","calender_01.tga","calender_10.tga","calender_11.tga"};
        const auto bytes=resources.data("grp",names[n]);const auto header=wa2::image::inspect_game_tga(bytes);
        const unsigned width=n<2?1280:n==2?472:728,height=n<2?720:n==2?48:32;
        std::cout<<names[n]<<' '<<header.width<<'x'<<header.height<<'\n';
        if(header.width<width || header.height<height)throw std::runtime_error("Original calendar atlas crop exceeds image dimensions: "+std::string(names[n]));++calendar_atlases;
    }
    unsigned animations{},animation_frames{},colour_maps{},selection_atlases{};
    for(unsigned n=0;n<2;++n){const auto bytes=resources.data("grp",n?"sys_00011.tga":"sys_00200.tga");const auto header=wa2::image::inspect_game_tga(bytes);
        if(header.width<(n?248u:784u) || header.height<(n?104u:216u))throw std::runtime_error("Original selection atlas crop exceeds image dimensions");++selection_atlases;}
    for(const auto name:{"nega.amp","sepia.amp"}){const auto bytes=resources.data("grp",name);wa2::image::parse_colour_map(bytes);++colour_maps;}
    for(unsigned n=0;n<3;++n){constexpr std::string_view names[]{"sakura1_2.tga","sakura2.tga","sakura3.tga"};constexpr unsigned cell[]{128,32,16};
        const auto header=wa2::image::inspect_game_tga(resources.data("grp",names[n]));if(header.width<12*cell[n] || header.height<2*cell[n])throw std::runtime_error("Original sakura atlas crop exceeds image dimensions");}
    for(bool introductory:{false,true})if(const auto* bank=resources.optional("grp",introductory))for(const auto& member:bank->members())if(member.name.ends_with(".ani")){
        const auto data=wa2::image::Animation::parse(bank->load(bank->find_first(member.name)));++animations;animation_frames+=unsigned(data->frames().size());
        std::vector<wa2::image::TgaLayout> headers;for(const auto& texture:data->textures())headers.push_back(wa2::image::inspect_game_tga(resources.data("grp",texture)));
        for(const auto& frame:data->frames())for(const auto& piece:frame.pieces){const auto& header=headers.at(piece.texture);
            if(unsigned(piece.source_x)+unsigned(piece.width)>header.width || unsigned(piece.source_y)+unsigned(piece.height)>header.height)throw std::runtime_error("Original animation source crop exceeds texture dimensions");}
    }
    std::ofstream report(argv[2]);report<<"{\"schema\":\"wa2.native-script-catalogue.v1\",\"scenes\":"<<scenes<<",\"labels\":"<<labels<<",\"command_sites\":"<<calls<<",\"command_indices\":[";
    bool first=true;for(auto id:commands){if(!first)report<<',';first=false;report<<id;}
    report<<"],\"menu_atlas_headers\":"<<menu_atlases<<",\"calendar_literal_calls\":"<<calendar_calls<<",\"calendar_literals_validated\":"<<calendar_literals<<",\"calendar_unknown_arguments\":"<<calendar_unknown<<",\"calendar_literal_year_201\":"<<calendar_201;
    report<<",\"calendar_atlas_headers\":"<<calendar_atlases<<",\"selection_atlas_headers\":"<<selection_atlases<<",\"sprite_animations\":"<<animations<<",\"animation_frames\":"<<animation_frames<<",\"colour_maps_parsed\":"<<colour_maps<<",\"animation_sequences_executed\":false,\"resource_metadata_and_script_data_only\":true,\"script_executed\":false,\"source_executable_opened\":false,\"screen_access\":false,\"game_launched\":false,\"whole_game_verified\":false,\"passed\":true}\n";
    report.close();if(!report)throw std::runtime_error("Cannot write original script structure report");
    std::cout<<"PASS static resource script structure: "<<scenes<<" scripts, "<<labels<<" labels, "<<calls<<" command sites; no gameplay executed.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
