#include "render.hpp"
#include "render_profile.hpp"
#include "../glyph_command_replay_control.hpp"
#include "../glyph_budget.hpp"
namespace wa2::special {
bool draw_sprite(wr::Cpu& cpu,wr::Memory& memory){
    return !drawing_source_reference && aot::native_draw_sprite(cpu,memory,drawing_source);
}
bool draw_glyph(wr::Cpu& cpu,wr::Memory& memory){
    return !drawing_source_reference && aot::native_draw_glyph_or_yield(cpu,memory,drawing_source);
}
std::string sprite_report(){
    const auto c=aot::native_sprite_counts();
    return "{\"accepted\":"+std::to_string(c.accepted)+",\"declined\":"+std::to_string(c.declined)+
        ",\"tiles\":"+std::to_string(c.tiles)+",\"offscreen\":"+std::to_string(c.offscreen)+
        ",\"fp_declined\":"+std::to_string(c.fp_declined)+",\"adapter_declined\":"+std::to_string(c.adapter_declined)+
        ",\"shared_main_implementation\":true}";
}
std::string glyph_report(){
    const auto c=aot::native_glyph_counts();
    return "{\"accepted\":"+std::to_string(c.accepted)+",\"declined\":"+std::to_string(c.declined)+
        ",\"tiles\":"+std::to_string(c.tiles)+",\"command_replay_hits\":"+std::to_string(aot::glyph_command_replay_hits)+
        ",\"command_replay_captures\":"+std::to_string(aot::glyph_command_replay_captures)+
        ",\"budget_deferrals\":"+std::to_string(aot::glyph_budget_deferrals)+
        ",\"shared_main_implementation\":true}";
}
}
