#pragma once
#include <array>
#include <cstdint>
#include <span>
namespace wr {struct Cpu;class Memory;}
namespace wa2::aot {
struct SpriteVertex {float x,y,z,rhw;std::uint32_t colour,secondary;float u,v;};
static_assert(sizeof(SpriteVertex)==32);
struct SpriteTile {std::uint32_t texture;std::array<SpriteVertex,4> vertices;};
enum class SpriteTarget : std::uint32_t {surface,texture_level_zero};
struct SpriteCommand {
    std::uint32_t device,target;
    SpriteTarget target_kind{SpriteTarget::surface};
    std::uint32_t blend_enabled,blend_op,source_blend,destination_blend;
    std::uint32_t minimum,magnification,mipmap,alpha_argument;
    std::span<const SpriteTile> tiles;
};
enum class SpriteState {render,stage,sampler};
template<class Set>void sprite_state_words(const SpriteCommand& c,Set set){
    set(SpriteState::render,27,c.blend_enabled);set(SpriteState::render,171,c.blend_op);
    set(SpriteState::render,19,c.source_blend);set(SpriteState::render,20,c.destination_blend);
    set(SpriteState::stage,1,4);set(SpriteState::stage,2,2);set(SpriteState::stage,3,0);set(SpriteState::stage,4,4);
    if(c.alpha_argument==1)set(SpriteState::stage,5,0);else{set(SpriteState::stage,5,2);set(SpriteState::stage,6,0);}
    set(SpriteState::sampler,5,c.minimum);set(SpriteState::sampler,6,c.magnification);set(SpriteState::sampler,7,c.mipmap);
    set(SpriteState::sampler,1,3);set(SpriteState::sampler,2,3);
}
// A typed native command, bound to one Memory lifetime and one owner thread.
// The presentation adapter may decline before changing state. It never calls
// guest code, pumps input, waits on guest threads or retains the packet span.
class SpriteService {
    inline static thread_local SpriteService* first_{};
    SpriteService* next_{};
    wr::Memory* memory_;
    bool (*submit_)(const SpriteCommand&,void*);
    void* owner_;
public:
    SpriteService(wr::Memory& memory,bool(*submit)(const SpriteCommand&,void*),void* owner):next_(first_),memory_(&memory),submit_(submit),owner_(owner){first_=this;}
    ~SpriteService(){auto** link=&first_;while(*link && *link!=this)link=&(*link)->next_;if(*link)*link=next_;}
    SpriteService(const SpriteService&)=delete;
    static SpriteService* find(wr::Memory& memory){for(auto* s=first_;s;s=s->next_)if(s->memory_==&memory)return s;return nullptr;}
    bool submit(const SpriteCommand& command){return submit_(command,owner_);}
};
struct NativeSpriteCounts {std::uint64_t accepted{},declined{},tiles{},offscreen{},capability_filter{},fp_declined{},adapter_declined{};};
NativeSpriteCounts native_sprite_counts();
bool native_draw_sprite(wr::Cpu&,wr::Memory&);
}
