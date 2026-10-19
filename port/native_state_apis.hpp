#pragma once
#include <winrecomp/runtime.hpp>
#include <memory>
namespace wr {
// Integer-only leaf APIs. No callbacks, waits, host handles or GPU work occur
// here; all other APIs continue through the ordinary checked bridge.
enum class Wa2StateApiKind : U32 {render=1,stage=2,sampler=3,fvf_same=4,target_same=5,texture_same=6,texture_binding=7};
struct Wa2StateApiBinding {
    U32* values{};
    U32 columns{},rows{},receiver{},kind{};
    std::uint64_t* calls{};
    std::uint64_t* version{};
    // Kinds 1-3: comparison baseline. Kind 7: live texture tokens indexed
    // by token >> 16; only exact matches may update the deferred bindings.
    const U32* baseline{};
};
struct Wa2StateApiTable {
    std::array<Wa2StateApiBinding,4096> entries{};
    std::uint64_t* api_calls{};
};
std::shared_ptr<Wa2StateApiTable> wa2_state_apis(Memory&,std::uint64_t*);
void wa2_bind_state_api(Wa2StateApiTable&,U32,U32,Wa2StateApiKind,U32*,U32,U32,std::uint64_t*,std::uint64_t* version=nullptr,const U32* baseline=nullptr);
void wa2_unbind_state_receiver(Wa2StateApiTable&,U32);
void wa2_unbind_state_api(Wa2StateApiTable&,U32);
}
