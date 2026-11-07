#pragma once
#include <array>
// Dimensions only. Tests generate constant-colour pixels in memory; this
// metadata does not contain or load original artwork.
namespace wa2::test {
struct OptionsAtlas {const char* name;unsigned w,h;};
inline constexpr std::array<OptionsAtlas,19> options_atlases{{
    {"sys_03000.tga",1280,36},{"sys_09000.tga",1280,56},{"sys_03010.tga",756,96},{"sys_03011.tga",576,96},
    {"sys_03020.tga",432,128},{"sys_03030.tga",208,72},{"sys_09100.tga",56,72},
    {"sys_03100.tga",1024,464},{"sys_03200.tga",1024,464},{"sys_03201.tga",1024,464},{"sys_03040.tga",944,216},
    {"sys_03101.tga",1024,464},{"sys_03210.tga",1024,464},{"sys_03211.tga",1024,464},{"sys_03041.tga",944,120},
    {"sys_03102.tga",1024,464},{"sys_03220.tga",1024,464},{"sys_03221.tga",1024,464},{"sys_03042.tga",944,24}
}};
}
