#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdlib>
#include <winrecomp/process.hpp>
#include "wa2_win32.hpp"
extern "C" const unsigned char wa2_image_begin[],wa2_image_end[];
bool compiled_step(wr::Cpu&,wr::Memory&,std::uint64_t&);
inline int wa2_entry(int argc,char** argv){
    const auto bytes=std::span(wa2_image_begin,static_cast<std::size_t>(reinterpret_cast<std::uintptr_t>(wa2_image_end)-reinterpret_cast<std::uintptr_t>(wa2_image_begin)));
    return wr::run_program(argc,argv,compiled_step,"246a263b965147a274fa59cdd7a72f6be10ba063d8cf4e61e74baefb56f29327",wr::install_wa2_win32,bytes);
}
int main(int argc,char** argv){return wa2_entry(argc,argv);}
int WINAPI WinMain(HINSTANCE,HINSTANCE,LPSTR,int){return wa2_entry(__argc,__argv);}
