#pragma once
#include "../platform/render/texture_batch.hpp"
#include "../platform/render/glyph_command_replay.hpp"
#include "../platform/render/prepared_geometry.hpp"
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
inline void check_glyph_pipeline_rollover(){
    namespace g=wa2::graphics;
    struct Resources {
        unsigned target{17},indices{};
        bool scene{true};
        std::array<unsigned,16> streams{},textures{};
    };
    struct Applied : Resources {std::size_t transform_count{1};};
    struct State : Resources {std::vector<unsigned> transforms{19};};
    Applied old;old.textures[0]=71;
    State next;next.textures[0]=83;
    const auto verify_state=[&](const State& candidate){
        return g::same_glyph_packet_state(old,candidate,true,0);
    };
    CHECK(verify_state(next));
    CHECK(!g::same_glyph_packet_state(old,next,false,0));
    CHECK(!g::same_glyph_packet_state(old,next,true,1));
    for(unsigned change=0;change<7;++change){
        auto changed=next;
        switch(change){
        case 0:++changed.target;break;
        case 1:changed.scene=false;break;
        case 2:++changed.indices;break;
        case 3:++changed.streams.back();break;
        case 4:changed.transforms.push_back(0);break;
        case 5:changed.textures[0]=0;break;
        case 6:++changed.textures.back();break;
        }
        CHECK(!verify_state(changed));
    }
    std::array<g::ScreenVertex,4> quad{};
    for(unsigned limit:{g::TextureBatchSlots,g::WideTextureBatchSlots}){
        g::ScreenTextureBatch<unsigned> batch;batch.set_slot_limit(limit);
        CHECK(!batch.can_roll_glyph_packet(true,true,limit,4,6));
        for(unsigned slot=0;slot<limit;++slot)
            CHECK(batch.append(1+slot,quad,g::abi::D3DPT_TRIANGLESTRIP,4));
        CHECK(!batch.fits(83,4,6));
        CHECK(batch.can_roll_glyph_packet(true,true,limit,4,6));
        CHECK(!batch.can_roll_glyph_packet(false,true,limit,4,6));
        CHECK(!batch.can_roll_glyph_packet(true,false,limit,4,6));
        CHECK(!batch.can_roll_glyph_packet(true,true,limit==8?16:8,4,6));
        CHECK(!batch.can_roll_glyph_packet(true,true,limit,batch.MaxVertices+1,6));
        CHECK(!batch.can_roll_glyph_packet(true,true,limit,4,batch.MaxIndices+1));

        // Draw the first packet, then validate the real bindings at the
        // following packet draw and on return to a general multitexture draw.
        std::array<unsigned,16> physical{};
        std::vector<unsigned> actual;
        const auto draw=[&]{
            for(unsigned slot=0;slot<limit;++slot)
                physical[slot]=slot<batch.binding_count()?batch.bindings()[slot]:0;
            for(std::size_t i=0;i<batch.indices().size();i+=6)
                actual.push_back(physical[unsigned(batch.vertices()[batch.indices()[i]].binding)]);
            batch.clear();
        };
        draw();
        unsigned checked_samplers=0;
        g::apply_logical_texture_units(next.textures,limit,[&](unsigned slot,unsigned resource){
            physical[slot]=resource;
            if(resource){CHECK(slot==0 && resource==83);++checked_samplers;}
        });
        CHECK(checked_samplers==1 && physical[0]==83 && physical[1]==2);
        CHECK(batch.append(83,quad,g::abi::D3DPT_TRIANGLESTRIP,4));
        CHECK(batch.append(42,quad,g::abi::D3DPT_TRIANGLESTRIP,4));
        draw();
        CHECK(actual.size()==limit+2);
        for(unsigned slot=0;slot<limit;++slot)CHECK(actual[slot]==slot+1);
        CHECK(actual[limit]==83 && actual.back()==42);
        std::array<unsigned,16> ordinary{};
        ordinary[2]=63;ordinary[limit-1]=83;
        g::apply_logical_texture_units(ordinary,0,[&](unsigned slot,unsigned resource){physical[slot]=resource;});
        CHECK(physical==ordinary);
        for(unsigned i=0;i<batch.MaxVertices/4;++i)
            CHECK(batch.append(11,quad,g::abi::D3DPT_TRIANGLESTRIP,4));
        CHECK(!batch.fits(11,4,6) && batch.can_roll_glyph_packet(true,true,limit,4,6));
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
inline void check_prepared_packet_geometry(){
    namespace g=wa2::graphics;
    g::ScreenTextureBatch<unsigned> source,actual;
    std::array<g::ScreenVertex,4> quad{};
    const auto append=[&](auto& batch,unsigned tex){CHECK(batch.append(tex,quad,g::abi::D3DPT_TRIANGLESTRIP,4));};
    append(source,10);append(source,20);append(source,20);append(source,30);
    const auto bytes=[](auto span){return std::span(reinterpret_cast<const std::uint8_t*>(span.data()),span.size_bytes());};
    g::ImmutableBuffers<unsigned,1,4096,4096> buffers;
    g::GeometryTicket ticket{};
    const auto upload=[&]{
        return buffers.acquire(bytes(source.vertices()),bytes(source.indices()),
            [](auto,auto){return 17u;},[](unsigned){},&ticket);
    };
    CHECK(!upload());CHECK(upload()==17u && ticket.generation!=0);
    const auto view=buffers.view(ticket);
    g::PreparedGlyphGeometry saved;
    auto plan=*g::screen_vertex_plan(0x1c4u,{0,0,1280,720,0,1},{0,1});
    saved.record({83,3,4,6,0,plan,{1,1},{}});
    saved.finish(ticket);
    CHECK(saved.find(83,3,plan,0) && !saved.find(83,2,plan,0) && !saved.find(83,3,plan,1));
    ++plan.viewport.Width;CHECK(!saved.find(83,3,plan,0));--plan.viewport.Width;
    const auto span=saved.find(83,3,plan,0);
    append(actual,42); // Reuse geometry while binding this scene's resources.
    const std::array<unsigned,2> tex{77,77};
    const std::array<std::uint8_t,2> wrong{1,2};
    CHECK(!actual.borrow(ticket,view,4,6,tex,wrong) && actual.binding_count()==1);
    CHECK(actual.borrow(ticket,view,span->vertex,span->element,tex,std::span(span->slots).first(2)));
    CHECK(actual.geometry_ticket()==ticket && actual.vertex_count()==12 && actual.element_count()==18);
    CHECK(actual.bindings()[0]==42 && actual.bindings()[1]==77 && buffers.reuse(ticket)==17u);
    CHECK(actual.borrow(ticket,view,12,18,std::array<unsigned,1>{88},std::array<std::uint8_t,1>{2}));
    CHECK(actual.element_count()==24 && actual.geometry_ticket()==ticket);
    // Failed preflight must leave the borrowed packet ready for its actual
    // draw. In particular, a texture rollover must not force a CPU copy.
    CHECK(!actual.append(99,quad,g::abi::D3DPT_TRIANGLESTRIP,5));
    CHECK(!actual.append_glyph_quad(0,plan,{}));
    CHECK(actual.geometry_ticket()==ticket && actual.element_count()==24);
    // An ordinary next draw keeps the complete previously admitted prefix.
    quad[2].position[0]=.25f;append(source,44);append(actual,44);
    CHECK(!actual.geometry_ticket().generation && std::ranges::equal(bytes(source.vertices()),bytes(actual.vertices())));
    CHECK(std::ranges::equal(source.indices(),actual.indices()));
    actual.clear();append(actual,42);
    CHECK(!actual.borrow(ticket,view,4,6,tex,std::span(span->slots).first(2))); // altered first quad
    buffers.clear([](unsigned){});
    CHECK(buffers.view(ticket).vertices.empty() && !buffers.reuse(ticket));
    auto old=ticket;CHECK(!upload());CHECK(upload()==17u && ticket.slot==old.slot && ticket!=old);
    CHECK(!buffers.reuse(old));
    source.clear();actual.clear();
    for(unsigned tex=1;tex<=8;++tex){append(source,tex);if(tex<8)append(actual,tex);}
    CHECK(!upload());CHECK(upload()==17u);
    CHECK(actual.borrow(ticket,buffers.view(ticket),28,42,std::array<unsigned,1>{8},std::array<std::uint8_t,1>{7}));
    std::array<wa2::aot::SpriteVertex,4> next{};
    CHECK(!actual.append_glyph_quad(9,plan,bytes(std::span(next))));
    CHECK(!actual.append(9,quad,g::abi::D3DPT_TRIANGLESTRIP,4));
    CHECK(actual.geometry_ticket()==ticket && actual.element_count()==48);
}
inline Result run(){
    namespace g=wa2::graphics;
    check_glyph_continuations();
    check_glyph_sampler_units();
    check_glyph_pipeline_rollover();
    check_prepared_packet_geometry();
    Result result;
    {
        std::array<wa2::aot::SpriteTile,4> tiles{};
        for(unsigned n=0;n<tiles.size();++n){
            tiles[n].texture=1+n/2;
            for(unsigned v=0;v<4;++v)tiles[n].vertices[v]={float(n+v),float(v),.25f,1,0x99ad0137,0,0.f,1.f};
        }
        wa2::aot::SpriteCommand command{};
        command.device=100;command.target=77;command.tiles=tiles;command.glyph=true;
        g::GlyphCommandState proof;
        proof.capture(command);CHECK(proof.matches(command));
        using Command=wa2::aot::SpriteCommand;
        for(auto member:{&Command::device,&Command::target,&Command::blend_enabled,&Command::blend_op,
                         &Command::source_blend,&Command::destination_blend,&Command::minimum,
                         &Command::magnification,&Command::mipmap,&Command::alpha_argument}){
            auto changed=command;++(changed.*member);CHECK(!proof.matches(changed));
        }
        auto empty=command;empty.tiles={};CHECK(!proof.matches(empty));
        auto other=command;other.glyph=false;CHECK(!proof.matches(other));
        std::array<wa2::aot::SpriteTile,5> oversized{};
        auto large=command;large.tiles=oversized;CHECK(!proof.matches(large));
        command.target_kind=wa2::aot::SpriteTarget::texture_level_zero;CHECK(!proof.matches(command));
        command.target_kind=wa2::aot::SpriteTarget::surface;
        proof.valid=false;CHECK(!proof.matches(command));
        const auto plan=g::screen_vertex_plan(0x1c4u,{0,0,1280,720,0,1},{0,1});CHECK(plan);
        g::ScreenTextureBatch<unsigned> candidate,reference;
        const std::array<unsigned,4> textures{1,1,2,2};
        CHECK(g::append_surface_glyph(candidate,*plan,tiles,std::span<const unsigned>(textures)));
        for(const auto& tile:tiles)CHECK(reference.append_glyph_quad(tile.texture,*plan,
            {reinterpret_cast<const std::uint8_t*>(tile.vertices.data()),sizeof(tile.vertices)}));
        CHECK(reference.bindings()==candidate.bindings() && std::ranges::equal(reference.indices(),candidate.indices()));
        CHECK(reference.vertices().size_bytes()==candidate.vertices().size_bytes());
        CHECK(!std::memcmp(reference.vertices().data(),candidate.vertices().data(),reference.vertices().size_bytes()));
        tiles.back().vertices.back().secondary=1;
        CHECK(!g::append_surface_glyph(candidate,*plan,tiles,std::span<const unsigned>(textures)));
        CHECK(candidate.indices().size()==24 && candidate.binding_count()==2);
        tiles.back().vertices.back().secondary=0;
        std::array<g::ScreenVertex,4> quad{};
        for(unsigned texture=3;texture<=7;++texture)CHECK(candidate.append(texture,quad,5,4));
        const auto previous=candidate.indices().size();
        const std::array<unsigned,4> overflow{1,2,8,9};
        CHECK(!g::append_surface_glyph(candidate,*plan,tiles,std::span<const unsigned>(overflow)));
        CHECK(candidate.indices().size()==previous && candidate.binding_count()==7);
        candidate.clear();reference.clear();
        for(unsigned alpha=0;alpha<256;++alpha){
            // During reveal the same font texture and state receive new source
            // positions and alpha on every call. Neither may be held over.
            for(auto& v:tiles[0].vertices){v.colour=alpha<<24;v.x+=.5f;}
            CHECK(g::append_surface_glyph(candidate,*plan,std::span(tiles).first(1),std::span<const unsigned>(textures).first(1)));
            const auto& vertices=tiles[0].vertices;
            CHECK(reference.append_glyph_quad(textures[0],*plan,{reinterpret_cast<const std::uint8_t*>(vertices.data()),sizeof(vertices)}));
        }
        CHECK(reference.vertices().size_bytes()==candidate.vertices().size_bytes());
        CHECK(!std::memcmp(reference.vertices().data(),candidate.vertices().data(),reference.vertices().size_bytes()));
        CHECK(std::ranges::equal(reference.indices(),candidate.indices()));
        struct Resource{unsigned interface_handle;};
        auto live=std::make_shared<Resource>(Resource{100});auto retained=live;unsigned lookups=0;
        g::GlyphTextureLookup<Resource> cache;
        const auto lookup=[&](unsigned token){CHECK(token==100);++lookups;return live;};
        for(unsigned n=0;n<500;++n)CHECK(cache.resolve(100,lookup)==live);
        CHECK(lookups==1);retained->interface_handle=0;live=std::make_shared<Resource>(Resource{100});
        CHECK(cache.resolve(100,lookup)==live && lookups==2 && live!=retained);
    }
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
