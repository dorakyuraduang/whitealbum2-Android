#pragma once
namespace wa2::test {
struct TitleAtlas {const char* name;unsigned w,h;};
// Dimensions only; finite tests generate their own uniform RGBA pixels.
inline constexpr TitleAtlas title_atlases[]{
    {"T0000.tga",1280,856},{"T0100.tga",640,640},{"T0010.tga",752,448},{"T0011.tga",752,448},
    {"T0020.tga",264,24},{"T0050.tga",408,24},{"T0101.tga",320,80},{"T0200.tga",288,320},{"T0201.tga",288,160}
};
}
