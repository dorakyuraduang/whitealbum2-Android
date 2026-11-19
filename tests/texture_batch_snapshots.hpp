#pragma once
#include "../platform/render/texture_batch.hpp"
#include <memory>
namespace texture_batch_test {
struct Result {unsigned source_draws{},driver_packets{},triangles{},reused_texture_packets{},lifetime_checks{};
    unsigned narrow_glyph_packets{},adaptive_glyph_packets{};};
inline void check_glyph_continuations() {
    namespace g = wa2::graphics;
    const auto plan = g::screen_vertex_plan(0x1c4u, {0, 0, 1280, 720, 0, 1}, {0, 1});
    CHECK(plan);
    g::ScreenTextureBatch<unsigned> expected, actual;
    unsigned full_packets = 0;
    const auto compare = [&] {
        CHECK(expected.binding_count() == actual.binding_count());
        CHECK(expected.bindings() == actual.bindings());
        CHECK(std::ranges::equal(expected.indices(), actual.indices()));
        const auto vertices = expected.vertices();
        CHECK(vertices.size() == actual.vertices().size());
        if (!vertices.empty())
            CHECK(std::memcmp(vertices.data(), actual.vertices().data(), vertices.size_bytes()) == 0);
    };
    std::array<std::uint8_t, 128> bytes{};
    std::array<g::ScreenVertex, 4> quad;
    // Repeated tiles, atlas changes and full packets over a complete page.
    for (unsigned n = 0; n < 676; ++n) {
        for (unsigned corner = 0; corner < 4; ++corner) {
            auto* v = bytes.data() + corner * 32;
            const std::array<float, 4> pos{float(n + corner), float(corner), .25f, 1.f};
            const std::array<float, 2> uv{float(corner) / 4, .75f};
            std::memcpy(v, pos.data(), 16);
            std::memcpy(v + 16, &n, 4);
            std::memcpy(v + 24, uv.data(), 8);
        }
        CHECK(g::pack_screen_vertices(*plan, bytes, 32, quad));
        const unsigned texture = 1 + n / 2 % 11;
        if (!expected.fits(texture, 4, 6)) {
            CHECK(!actual.append_glyph_quad(texture, *plan, bytes));
            compare();
            expected.clear();
            actual.clear();
            ++full_packets;
        }
        CHECK(expected.append(texture, quad, g::abi::D3DPT_TRIANGLESTRIP, 4));
        CHECK(actual.append_glyph_quad(texture, *plan, bytes));
    }
    CHECK(full_packets != 0);
    compare();
    CHECK(!actual.append_glyph_quad(0, *plan, bytes));
    CHECK(!actual.append_glyph_quad(1, *plan, std::span(bytes).first(96)));
    bytes[3 * 32 + 20] = 1;
    CHECK(!actual.append_glyph_quad(actual.bindings()[0], *plan, bytes));
    compare();
}
inline void check_glyph_sampler_units() {
    namespace g = wa2::graphics;
    std::array<unsigned, 16> logical{};
    logical[0] = 100;
    for (auto limit : {g::TextureBatchSlots,g::WideTextureBatchSlots})
    for (unsigned used = 1; used <= limit; ++used) {
        std::array<unsigned, 16> reference, candidate;
        reference.fill(42);
        candidate = reference;
        unsigned immediate = 0, deferred = 0;
        g::apply_logical_texture_units(logical, 0, [&](unsigned n, unsigned value) {
            reference[n] = value;
            ++immediate;
        });
        g::apply_logical_texture_units(logical, limit, [&](unsigned n, unsigned value) {
            candidate[n] = value;
            ++deferred;
        });
        CHECK(immediate == logical.size() && immediate - deferred == limit - 1);
        CHECK(candidate[0] == logical[0]);
        for (unsigned n = 0; n < limit; ++n) {
            const auto packet_texture = n < used ? 90u + n : 0u;
            candidate[n] = reference[n] = packet_texture;
        }
        CHECK(candidate == reference);
    }
}
inline std::vector<unsigned> replay_glyph_packets(const std::vector<unsigned>& textures,unsigned supported){
    namespace g=wa2::graphics;
    g::ScreenTextureBatch<unsigned> batch;
    std::vector<unsigned> occupancies;
    unsigned source=0;
    const auto flush=[&]{
        if(batch.empty())return;
        occupancies.push_back(batch.binding_count());
        const std::array<unsigned,6> corners{0,1,2,2,1,3};
        unsigned index=0;
        for(auto vertex_index:batch.indices()){
            const auto& vertex=batch.vertices()[vertex_index];
            CHECK(source<textures.size() && batch.bindings()[unsigned(vertex.binding)]==textures[source]);
            CHECK(vertex.screen.position[0]==float(4*source+corners[index]));
            if(++index==corners.size()){index=0;++source;}
        }
        CHECK(index==0);batch.clear();
    };
    for(unsigned draw=0;draw<textures.size();++draw){
        const auto texture=textures[draw];
        if(!batch.fits(texture,4,6)){
            const auto next=batch.slots_after_glyph_rollover(texture,supported);
            flush();batch.set_slot_limit(next);
        }
        std::array<g::ScreenVertex,4> quad{};
        for(unsigned n=0;n<quad.size();++n)quad[n].position[0]=float(4*draw+n);
        CHECK(batch.append(texture,quad,g::abi::D3DPT_TRIANGLESTRIP,4));
    }
    flush();CHECK(source==textures.size());return occupancies;
}
inline Result run(){
    namespace g=wa2::graphics;
    check_glyph_continuations();
    check_glyph_sampler_units();
    Result result;
    CHECK(g::supported_texture_batch_slots(32,false)==0 && g::supported_texture_batch_slots(7,true)==0);
    CHECK(g::supported_texture_batch_slots(8,true)==8 && g::supported_texture_batch_slots(15,true)==8);
    CHECK(g::supported_texture_batch_slots(16,true)==16 && g::supported_texture_batch_slots(32,true)==16);
    std::vector<unsigned> page(676);
    for(unsigned n=0;n<page.size();++n)page[n]=1+n/2%11;
    result.narrow_glyph_packets=unsigned(replay_glyph_packets(page,8).size());
    result.adaptive_glyph_packets=unsigned(replay_glyph_packets(page,16).size());
    CHECK(result.narrow_glyph_packets==43 && result.adaptive_glyph_packets==2);
    for(unsigned n=0;n<page.size();++n)page[n]=1+n%8;
    CHECK((replay_glyph_packets(page,16)==std::vector<unsigned>{8}));
    page.resize(33);for(unsigned n=0;n<page.size();++n)page[n]=n+1;
    CHECK((replay_glyph_packets(page,16)==std::vector<unsigned>{8,16,9}));
    struct Expected {unsigned texture;g::ScreenVertex vertex;};
    std::vector<Expected> original,submitted;
    for(unsigned primitive:{4u,5u,6u}){
        g::ScreenTextureBatch<unsigned> batch;
        const auto flush=[&]{
            if(batch.empty())return;++result.driver_packets;
            for(auto index:batch.indices()){
                CHECK(index<batch.vertices().size());const auto& vertex=batch.vertices()[index];
                const auto slot=unsigned(vertex.binding);CHECK(float(slot)==vertex.binding && slot<batch.binding_count());
                submitted.push_back({batch.bindings()[slot],vertex.screen});
            }
            result.triangles+=unsigned(batch.indices().size()/3);batch.clear();
        };
        for(unsigned draw=0;draw<40;++draw){
            std::array<g::ScreenVertex,4> vertices{};
            for(unsigned n=0;n<4;++n){auto& v=vertices[n];v.position={float(draw*4+n),float(37-n*3),.25f,n?.5f:1.f};
                v.colour={std::uint8_t(draw*5),std::uint8_t(n*61),std::uint8_t(draw+n),std::uint8_t(255-n*43)};
                v.texture={{{float(n)*.25f-.125f,float(draw)*.125f},{float(draw)*-.25f,float(n)+.5f}}};
            }
            const auto texture=draw+1,count=primitive==4?3u:4u;
            if(!batch.fits(texture,4,g::ScreenTextureBatch<unsigned>::index_count(primitive,count)))flush();
            CHECK(batch.append(texture,vertices,primitive,count));++result.source_draws;
            const std::vector<unsigned> indices=primitive==4?std::vector<unsigned>{0,1,2}:
                primitive==5?std::vector<unsigned>{0,1,2,2,1,3}:std::vector<unsigned>{0,1,2,0,2,3};
            for(auto index:indices)original.push_back({texture,vertices[index]});
        }
        flush();
    }
    CHECK(result.source_draws==120 && result.driver_packets==15 && original.size()==submitted.size());
    for(unsigned n=0;n<original.size();++n){CHECK(original[n].texture==submitted[n].texture);
        CHECK(!std::memcmp(&original[n].vertex,&submitted[n].vertex,sizeof(g::ScreenVertex)));}
    g::ScreenTextureBatch<unsigned> shared;
    std::array<g::ScreenVertex,4> quad{};
    for(unsigned n=0;n<40;++n)CHECK(shared.append(9,quad,6,4));
    CHECK(shared.binding_count()==1 && shared.vertices().size()==160);++result.reused_texture_packets;shared.clear();
    std::array<std::uint32_t,4> indexed{3,2,1,0};CHECK(shared.append(3,quad,6,4,indexed));
    CHECK((std::vector<std::uint16_t>(shared.indices().begin(),shared.indices().end())==std::vector<std::uint16_t>{3,2,1,3,1,0}));
    const auto before=shared.vertices().size(),before_indices=shared.indices().size();indexed[0]=4;
    CHECK(!shared.append(4,quad,6,4,indexed));CHECK(!shared.append(4,quad,6,0xffffffff));CHECK(!shared.append(4,quad,1,4));
    CHECK(shared.vertices().size()==before && shared.indices().size()==before_indices && shared.binding_count()==1);shared.clear();
    for(unsigned n=0;n<1024;++n)CHECK(shared.append(1,quad,6,4));
    CHECK(shared.vertices().size()==4096 && !shared.append(1,quad,6,4));shared.clear();
    auto texture=std::make_shared<unsigned>(7);std::weak_ptr<unsigned> alive=texture;
    g::ScreenTextureBatch<std::shared_ptr<unsigned>> lifetime;
    CHECK(lifetime.append(texture,quad,6,4));texture.reset();CHECK(!alive.expired());++result.lifetime_checks;
    lifetime.clear();CHECK(alive.expired());++result.lifetime_checks;
    return result;
}
}
