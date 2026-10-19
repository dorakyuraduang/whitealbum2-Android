#pragma once
namespace wr {struct Cpu;class Memory;}
namespace wa2::aot {
#if WA2_AOT_DIAGNOSTICS
extern thread_local bool native_render_math_enabled;
extern thread_local bool native_vertex_grid_enabled;
class NativeVertexGridReferenceScope {
    bool previous_{native_vertex_grid_enabled};
public:
    NativeVertexGridReferenceScope(){native_vertex_grid_enabled=false;}
    ~NativeVertexGridReferenceScope(){native_vertex_grid_enabled=previous_;}
};
class NativeRenderMathReferenceScope {
    bool previous_{native_render_math_enabled};
public:
    NativeRenderMathReferenceScope(){native_render_math_enabled=false;}
    ~NativeRenderMathReferenceScope(){native_render_math_enabled=previous_;}
};
#endif
// Generated callers have already proved the complete owning source function.
// Declining a bounded-input contract leaves CPU and RAM untouched.
bool native_uv_columns(wr::Cpu&,wr::Memory&);
bool native_uv_rows(wr::Cpu&,wr::Memory&);
bool native_vertex_interpolation(wr::Cpu&,wr::Memory&);
bool native_vertex_grid(wr::Cpu&,wr::Memory&);
bool native_sprite_geometry(wr::Cpu&,wr::Memory&);
bool native_snow_motion(wr::Cpu&,wr::Memory&);
}
