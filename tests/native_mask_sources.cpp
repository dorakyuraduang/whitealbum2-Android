// Only writes generated shader text. No SDL, GL context or game is initialized.
#include "mask_shaders.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
int main(int argc,char** argv){try{
    CHECK(argc==2);const auto directory=std::filesystem::path(argv[1]);std::filesystem::create_directories(directory);
    for(bool embedded:{false,true}){const auto sources=wa2::platform::mask_shader_sources(embedded);const auto prefix=std::string("native-mask-")+(embedded?"embedded":"desktop");
        CHECK(sources.vertex.starts_with(embedded?"#version 100\n":"#version 120\n") && sources.fragment.starts_with(embedded?"#version 100\n":"#version 120\n"));
        std::ofstream vertex(directory/(prefix+".vert")),fragment(directory/(prefix+".frag"));vertex<<sources.vertex;fragment<<sources.fragment;vertex.close();fragment.close();CHECK(vertex && fragment);
    }
    std::cout<<"Generated both native mask shader profiles without a device.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
