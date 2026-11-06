// Parse original resource records only. No script is executed, scene created,
// game started, image decoded, device opened or source executable inspected.
#include "script_runtime.hpp"
#include "file_io.hpp"
#include "resource_catalog.hpp"
#include "sprite_animation.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>

int main(int argc,char** argv){try{
    if(argc!=3)throw std::runtime_error("usage: native_script_catalogue resource-directory report.json");
    const auto path=std::filesystem::path(argv[1])/"ck-gal.pak";
    auto archive=wa2::resource::Archive::open(wa2::platform::open_resource_file(path));wa2::script::Library library(archive);
    unsigned scenes{},labels{},calls{};std::set<std::uint32_t> commands;
    for(const auto& member:archive.members()){
        if(!member.name.ends_with(".bnr"))continue;
        const auto name=member.name.substr(0,member.name.size()-4);const auto program=library.load(name);++scenes;
        labels+=unsigned(program->labels().size());
        for(const auto& label:program->labels())if(!program->instruction_offset(label.offset))throw std::runtime_error("Original script label boundary mismatch");
        for(const auto& call:program->commands()){
            library.functions().at(call.index);commands.insert(call.index);++calls;
            if(!program->instruction_offset(call.offset))throw std::runtime_error("Original script command boundary mismatch");
        }
    }
    if(!scenes || !calls)throw std::runtime_error("Missing original script catalogue");
    wa2::resource::Catalog resources([&](std::string_view name)->std::shared_ptr<wa2::resource::Source>{
        const auto file=std::filesystem::path(argv[1])/std::filesystem::path(name);
        if(!std::filesystem::exists(file))return {};return wa2::platform::open_resource_file(file);
    });
    unsigned calendar_atlases{};
    for(unsigned n=0;n<4;++n){
        constexpr std::string_view names[]{"calender_00.tga","calender_01.tga","calender_10.tga","calender_11.tga"};
        const auto bytes=resources.data("grp",names[n]);const auto header=wa2::image::inspect_game_tga(bytes);
        const unsigned width=n<2?1280:n==2?472:728,height=n<2?720:n==2?48:32;
        std::cout<<names[n]<<' '<<header.width<<'x'<<header.height<<'\n';
        if(header.width<width || header.height<height)throw std::runtime_error("Original calendar atlas crop exceeds image dimensions: "+std::string(names[n]));++calendar_atlases;
    }
    unsigned animations{},animation_frames{};
    for(bool introductory:{false,true})if(const auto* bank=resources.optional("grp",introductory))for(const auto& member:bank->members())if(member.name.ends_with(".ani")){
        const auto data=wa2::image::Animation::parse(bank->load(bank->find_first(member.name)));++animations;animation_frames+=unsigned(data->frames().size());
        std::vector<wa2::image::TgaLayout> headers;for(const auto& texture:data->textures())headers.push_back(wa2::image::inspect_game_tga(resources.data("grp",texture)));
        for(const auto& frame:data->frames())for(const auto& piece:frame.pieces){const auto& header=headers.at(piece.texture);
            if(unsigned(piece.source_x)+unsigned(piece.width)>header.width || unsigned(piece.source_y)+unsigned(piece.height)>header.height)throw std::runtime_error("Original animation source crop exceeds texture dimensions");}
    }
    std::ofstream report(argv[2]);report<<"{\"schema\":\"wa2.native-script-catalogue.v1\",\"scenes\":"<<scenes<<",\"labels\":"<<labels<<",\"command_sites\":"<<calls<<",\"command_indices\":[";
    bool first=true;for(auto id:commands){if(!first)report<<',';first=false;report<<id;}
    report<<"],\"calendar_atlas_headers\":"<<calendar_atlases<<",\"sprite_animations\":"<<animations<<",\"animation_frames\":"<<animation_frames<<",\"animation_sequences_executed\":false,\"resource_metadata_and_script_data_only\":true,\"script_executed\":false,\"source_executable_opened\":false,\"screen_access\":false,\"game_launched\":false,\"whole_game_verified\":false,\"passed\":true}\n";
    report.close();if(!report)throw std::runtime_error("Cannot write original script structure report");
    std::cout<<"PASS static resource script structure: "<<scenes<<" scripts, "<<labels<<" labels, "<<calls<<" command sites; no gameplay executed.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
