#include "native_backend.hpp"
namespace wr {
namespace {
#include "native_backend_test_data.inc"
}
std::span<const Wa2NativeForwardRegion> wa2_native_forward_regions(){return native_forward_regions;}
std::span<const Wa2NativeInstruction> wa2_native_instructions(){return native_instructions;}
}
