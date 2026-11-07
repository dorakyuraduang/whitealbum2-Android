#pragma once
namespace wa2::test {
struct SaveAtlas {const char* name;unsigned w,h;};
// Resource metadata only. Tests allocate uniform pixels with these sizes.
inline constexpr SaveAtlas save_atlases[]{
    {"sys_01000.tga",1280,40},{"sys_02000.tga",1280,40},{"sys_09000.tga",1280,56},
    {"sys_01010.tga",1264,192},{"sys_01011.tga",632,96},{"sys_02011.tga",632,96},
    {"sys_01014.tga",160,20},{"sys_01020.tga",840,96},{"sys_01021.tga",640,96},
    {"sys_01013.tga",192,32},{"sys_01022.tga",432,128},{"sys_09100.tga",56,72},
    {"sys_01015.tga",120,96},{"sys_01016.tga",248,384},{"sys_04010.tga",304,168},
    {"sys_05010.tga",304,184},{"sys_05011.tga",304,184},{"sys_05012.tga",304,184},
    {"sys_00000.tga",1248,208},{"sys_02030.tga",320,56},{"sys_02031.tga",112,16},
    {"sys_02050.tga",432,312},{"sys_00011.tga",408,288},{"sys_00100.tga",544,216},{"sys_00101.tga",320,120}
};
}
