#pragma once
#include <string>
namespace wa2::graphics {
// Alpha testing is a pipeline operation, including with original custom pixel
// shaders. The wrapper runs after the shader's original main, without reserving
// or changing any original D3D constant register.
std::string alpha_test_source(std::string source);
}
