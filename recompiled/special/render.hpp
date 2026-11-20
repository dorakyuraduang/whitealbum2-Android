#pragma once
#include "../native_sprites.hpp"
#include "../native_glyphs.hpp"
#include <string>
namespace wa2::special {
// Finite memory checks can exercise the complete original static bodies.
inline thread_local bool drawing_source_reference{};
struct DrawingReferenceScope {
    bool previous{drawing_source_reference};
    DrawingReferenceScope(){drawing_source_reference=true;}
    ~DrawingReferenceScope(){drawing_source_reference=previous;}
};
bool draw_sprite(wr::Cpu&,wr::Memory&);
bool draw_glyph(wr::Cpu&,wr::Memory&);
bool sort_objects(wr::Cpu&,wr::Memory&);
std::string sprite_report();
std::string glyph_report();
std::string sort_report();
}
