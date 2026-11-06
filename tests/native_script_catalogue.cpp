// Parse original resource records only. No script is executed, scene created,
// game started, image decoded, device opened or source executable inspected.
#include "script_runtime.hpp"
#include "file_io.hpp"
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
    std::ofstream report(argv[2]);report<<"{\"schema\":\"wa2.native-script-catalogue.v1\",\"scenes\":"<<scenes<<",\"labels\":"<<labels<<",\"command_sites\":"<<calls<<",\"command_indices\":[";
    bool first=true;for(auto id:commands){if(!first)report<<',';first=false;report<<id;}
    report<<"],\"resource_metadata_and_script_data_only\":true,\"script_executed\":false,\"source_executable_opened\":false,\"screen_access\":false,\"game_launched\":false,\"whole_game_verified\":false,\"passed\":true}\n";
    report.close();if(!report)throw std::runtime_error("Cannot write original script structure report");
    std::cout<<"PASS static resource script structure: "<<scenes<<" scripts, "<<labels<<" labels, "<<calls<<" command sites; no gameplay executed.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
