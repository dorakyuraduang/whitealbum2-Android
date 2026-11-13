#include "text_work.hpp"
#include "native_glyphs.hpp"
#include "glyph_budget.hpp"
#include "text_jump_budget.hpp"
#include "../platform/render/text_buffers.hpp"
#include <sstream>
namespace wa2::aot {
std::string text_work_snapshot(){
    const auto g=native_glyph_counts();const auto e=glyph_entry_counts();const auto& t=text_jump_counts;
    const auto r=wa2::graphics::text_render_probe();
    std::ostringstream s;s<<"{";bool comma=false;
    const auto put=[&](const char* name,std::uint64_t value){if(comma)s<<',';comma=true;s<<'\"'<<name<<"\":"<<value;};
    put("presents",r.presents);put("monotonic_ns",r.monotonic_ns);put("text_entries",t.fresh_entries);put("text_resumes",t.resumed_entries);
    put("text_jumps",t.local_jumps);put("text_yields",t.budget_yields);put("text_external_targets",t.external_targets);
    put("text_pc24_entries",t.pc24);put("text_pc53_entries",t.pc53);put("text_pc64_entries",t.pc64);put("text_reserved_precision_entries",t.pc_reserved);
    put("glyph_attempts",e.attempts);put("glyph_accepted",g.accepted);put("glyph_declined",g.declined);
    put("glyph_precondition_declines",e.precondition_declines);put("glyph_tiles",g.tiles);
    put("glyph_budget_yields",glyph_budget_deferrals);put("wide_integer_glyphs",native_wide_glyph_calls);
    put("source_draws",r.draws);put("driver_draws",r.submitted);put("texture_packets",r.packets);
    put("glyph_packets",r.glyph_packets);put("state_applies",r.state_applies);put("uploads",r.uploads);
    put("function_entries",r.function_entries);put("api_calls",r.api_calls);
    put("live_objects",r.live_objects);put("retained_packets",r.retained_packets);put("retained_bytes",r.retained_bytes);
    // These timers overlap; callers must compare them, never add them up.
    put("draw_inclusive_ns",r.draw_ns);put("apply_inclusive_ns",r.apply_ns);put("flush_ns",r.flush_ns);put("upload_ns",r.upload_ns);
    put("swap_ns",r.swap_ns);put("function_inclusive_ns",r.function_ns);put("api_inclusive_ns",r.api_ns);put("sleep_ns",r.sleep_ns);
    put("source_fp_control",r.fp_control);s<<'}';return s.str();
}
}
