#pragma once
#include <string>
namespace wa2::platform {
struct MaskShaderSources {std::string vertex,fragment;};
// The equations are shared; only the desktop/GLES language preamble differs.
MaskShaderSources mask_shader_sources(bool embedded);
}
