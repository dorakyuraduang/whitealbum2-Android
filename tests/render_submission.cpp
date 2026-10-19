#include "../platform/render/submission_cache.hpp"
#include "../platform/render/screen_vertices.hpp"
#include "../platform/render/frame_metrics.hpp"
#include "../platform/window/menu_update.hpp"
#include <fstream>
#include <iostream>
#include <map>
#include <vector>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
#include "texture_batch_snapshots.hpp"
namespace g=wa2::graphics;namespace w=wa2::window;
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned checks{};const auto texture_batch=texture_batch_test::run();
    for(unsigned primitive=1;primitive<=6;++primitive){
        CHECK(g::batch_fixed_vertex_stage(false,primitive)==(primitive>=4));
        CHECK(!g::batch_fixed_vertex_stage(true,primitive));checks+=2;
    }
    // These queries expose metadata; pixel locks and writes remain barriers.
    // This checks the shared boundary policy, not a GPU or whole-scene batch.
    CHECK(g::texture_metadata_method(18) && g::surface_metadata_method(12));
    for(unsigned slot:{19u,20u,21u})CHECK(!g::texture_metadata_method(slot));
    for(unsigned slot:{13u,14u})CHECK(!g::surface_metadata_method(slot));checks+=6;
    unsigned packed_cases{},triangle_cases{};
    for(bool specular:{false,true})for(unsigned textures=0;textures<=2;++textures)for(unsigned padding:{0u,4u,12u})for(unsigned mapping=0;mapping<8;++mapping){
        const g::abi::D3DVIEWPORT9 viewport{17,23,1280,720,.125f,.875f};
        const auto plan=g::screen_vertex_plan(0x44u|(textures<<8)|(specular?0x80u:0u),viewport,{mapping,(mapping+1)%8});CHECK(plan);
        const auto prefix=specular?24u:20u,stride=prefix+textures*8+padding;std::vector<std::uint8_t> bytes(stride*4,0xa5);
        std::array<g::ScreenVertex,4> vertices;
        for(unsigned n=0;n<4;++n){
            const std::array<float,4> position{float(n*33)-.75f,float(n*17)-.25f,float(n)*.125f,n?.5f:0.f};
            auto* source=bytes.data()+stride*n;std::memcpy(source,position.data(),16);
            for(unsigned c=0;c<4;++c)source[16+c]=std::uint8_t(n*53+c*29);
            if(specular){const std::uint32_t secondary=std::uint32_t(n*37)<<24;std::memcpy(source+20,&secondary,4);}
            for(unsigned t=0;t<textures;++t){const std::array<float,2> uv{float(t+n)*.25f,-float(n+t)*.125f};std::memcpy(source+prefix+t*8,uv.data(),8);}
        }
        CHECK(g::pack_screen_vertices(*plan,bytes,stride,vertices));
        for(unsigned n=0;n<4;++n){const auto* source=bytes.data()+n*stride;std::array<float,4> raw;std::memcpy(raw.data(),source,16);
            const float w=raw[3]?1.f/raw[3]:1.f;
            const std::array<float,4> expected{((raw[0]+.5f-17)*2/1280-1)*w,((raw[1]+.5f-23)*2/720-1)*w,(2*raw[2]-1)*w,w};
            CHECK(std::memcmp(vertices[n].position.data(),expected.data(),16)==0);
            CHECK((vertices[n].colour==std::array<std::uint8_t,4>{source[18],source[17],source[16],source[19]}));
            for(unsigned unit=0;unit<2;++unit){std::array<float,2> expected_uv{};if(plan->coordinates[unit]<textures)std::memcpy(expected_uv.data(),source+prefix+plan->coordinates[unit]*8,8);
                CHECK(vertices[n].texture[unit]==expected_uv);}
        }
        auto unchanged=vertices;CHECK(!g::pack_screen_vertices(*plan,bytes,0,vertices) && std::memcmp(vertices.data(),unchanged.data(),sizeof(vertices))==0);
        CHECK(!g::pack_screen_vertices(*plan,bytes,stride,std::span(vertices).first(3)));++packed_cases;
        if(specular){bytes[20]=1;CHECK(!g::pack_screen_vertices(*plan,bytes,stride,vertices));CHECK(!std::memcmp(vertices.data(),unchanged.data(),sizeof(vertices)));}
    }
    for(const auto fvf:{0x42u,0x54u,0xe4u,0x344u,0x10044u})CHECK(!g::screen_vertex_plan(fvf,{0,0,1280,720,0,1},{0,1}));
    for(unsigned type:{4u,5u,6u})for(unsigned count=0;count<17;++count){
        std::vector<unsigned> indices;g::each_triangle(type,count,[&](unsigned a,unsigned b,unsigned c){indices.insert(indices.end(),{a,b,c});});
        std::vector<unsigned> expected;
        if(type==4)for(unsigned n=0;n+2<count;n+=3)expected.insert(expected.end(),{n,n+1,n+2});
        if(type==5){for(unsigned n=2;n<count;++n){if(n&1)expected.insert(expected.end(),{n-1,n-2,n});else expected.insert(expected.end(),{n-2,n-1,n});}}
        if(type==6)for(unsigned n=2;n<count;++n)expected.insert(expected.end(),{0,n-1,n});
        CHECK(indices==expected);++triangle_cases;
    }
    const auto mode=[&](bool valid,bool same,std::uint64_t words,std::uint64_t textures,bool fixed,unsigned mask,unsigned previous){
        ++checks;return g::submission_apply(valid,same,words,textures,fixed,std::uint16_t(mask),std::uint16_t(previous));};
    CHECK(mode(false,true,0,0,true,1,1)==g::SubmissionApply::full);
    CHECK(mode(true,false,0,0,true,1,1)==g::SubmissionApply::full);
    CHECK(mode(true,true,1,0,true,1,1)==g::SubmissionApply::full);
    CHECK(mode(true,true,0,0,true,1,1)==g::SubmissionApply::reuse);
    CHECK(mode(true,true,0,1,true,1,1)==g::SubmissionApply::textures);
    for(unsigned bit=0;bit<8;++bit){
        CHECK(mode(true,true,0,1,true,1u<<bit,0)==g::SubmissionApply::full);
        CHECK(mode(true,true,0,1,true,0,1u<<bit)==g::SubmissionApply::full);
        CHECK(mode(true,true,0,1,false,1u<<bit,0)==g::SubmissionApply::textures);
    }
    // Replay the production texture refresh against an in-memory driver.
    // A shared image must receive the last bound unit's sampler parameters;
    // replacing an alias must also restore the unchanged lower unit.
    std::array<unsigned,16> old{},current{},bound{},sampler{};std::map<unsigned,unsigned> object_parameters;
    old[0]=old[1]=7;current[0]=7;current[1]=8;bound=old;sampler[0]=1;sampler[1]=2;object_parameters[7]=2;
    std::vector<unsigned> order;
    const auto bind=[&](unsigned unit,unsigned image){bound[unit]=image;object_parameters[image]=sampler[unit];order.push_back(unit);};
    const auto unbind=[&](unsigned unit){bound[unit]=0;order.push_back(unit);};
    g::refresh_texture_bindings(current,old,bind,unbind);
    CHECK((order==std::vector<unsigned>{0,1}) && object_parameters[7]==1 && object_parameters[8]==2 && bound==current);++checks;
    old=current;current[1]=7;g::refresh_texture_bindings(current,old,bind,unbind);CHECK(object_parameters[7]==2);++checks;
    old=current;current[1]=0;g::refresh_texture_bindings(current,old,bind,unbind);CHECK(bound==current && object_parameters[7]==1);++checks;
    unsigned full{},textures{};bool valid=false;old={};bound={};std::vector<unsigned> sampled;
    for(unsigned n=0;n<128;++n){current={};current[0]=1+n%3;
        const auto selected=mode(valid,true,0,1,true,g::texture_mask(current),g::texture_mask(old));
        if(selected==g::SubmissionApply::full)++full;else{CHECK(selected==g::SubmissionApply::textures);++textures;}
        g::refresh_texture_bindings(current,old,bind,unbind);sampled.push_back(bound[0]);old=current;valid=true;
    }
    CHECK(full==1 && textures==127);for(unsigned n=0;n<sampled.size();++n)CHECK(sampled[n]==1+n%3);
    for(const unsigned slot:{3u,18u,38u,58u,64u,66u,68u,88u,90u,95u,108u,110u}){CHECK(g::cpu_only_device_method(slot));++checks;}
    for(const unsigned slot:{16u,17u,23u,30u,31u,32u,34u,35u,42u,43u,500u}){CHECK(!g::cpu_only_device_method(slot));++checks;}
    for(const unsigned slot:{37u,57u,65u,67u,69u,83u,84u,89u,94u,99u,107u,114u}){CHECK(g::direct_device_method(slot));++checks;}
    for(const unsigned slot:{8u,11u,12u,16u,17u,23u,42u,43u,500u}){CHECK(!g::direct_device_method(slot));++checks;}
    for(const unsigned slot:{44u,47u,57u,67u,69u,75u,94u,99u,110u,114u}){CHECK(g::context_free_device_method(slot));++checks;}
    for(const unsigned slot:{17u,37u,39u,65u,83u,84u,87u,89u,92u,100u,104u,107u,500u}){CHECK(!g::context_free_device_method(slot));++checks;}
    unsigned pending{},submissions{};for(unsigned n=0;n<128;++n){++pending;
        for(const auto query:{64u,58u})if(!g::cpu_only_device_method(query)){++submissions;pending=0;}}
    if(pending)++submissions;CHECK(submissions==1);++checks;
    std::array<std::uint8_t,16> constants{},source{};
    std::array<std::uint32_t,4> bits{0,0x7fc00123,0xff800000,0x3f800000};std::memcpy(source.data(),bits.data(),16);
    CHECK(g::update_shader_constants(constants,source));CHECK(!g::update_shader_constants(constants,source));
    source[3]=0x80;CHECK(g::update_shader_constants(constants,source) && constants[3]==0x80);
    source[4]^=1;CHECK(g::update_shader_constants(constants,source) && constants==source);
    CHECK(!g::update_shader_constants({},{}));bool rejected=false;
    try{g::update_shader_constants(constants,std::span(source).first(8));}catch(const std::invalid_argument&){rejected=true;}CHECK(rejected);checks+=6;
    g::FrameMetrics metrics;
    for(unsigned n=0;n<300;++n){const auto at=1000000000ull+std::uint64_t(n)*16666667;
        metrics.draw=100;metrics.apply=40;metrics.flush=20;metrics.upload=10;
        const std::uint64_t count=n+1;
        metrics.presented(n+1,at-600,at-300,at-290,at,n*7,(n+1)*3,n+1,{count*5,count*7,count*3,count*1000,count*2000,count*2,count,count*3000,count*4000,count*5000});
    }
    CHECK(metrics.frames==300 && metrics.intervals==299 && metrics.recent.size()==256 && metrics.recent.front().frame==45);
    CHECK(metrics.interval_total==299ull*16666667 && metrics.interval_histogram[1]==299 && metrics.interval_max==16666667);
    CHECK((metrics.total_stages==std::array<std::uint64_t,7>{30000,12000,6000,3000,2093,3000,87000}));
    CHECK(metrics.recent.back().copy==7 && metrics.recent.back().draws==3 && metrics.draw==0);checks+=4;
    CHECK((metrics.recent.back().work==std::array<std::uint64_t,10>{5,7,3,1000,2000,2,1,3000,4000,5000}));++checks;
    w::MenuTemplate menu;menu.items.push_back({10,0,0,0,u"Menu",{{11,0,0,0,u"Item",{}}}});
    auto changed=menu;CHECK(w::menu_update(menu.items,changed.items)==w::MenuUpdate::none);
    changed.items[0].children[0].state=8;CHECK(w::menu_update(menu.items,changed.items)==w::MenuUpdate::state);
    changed.items[0].children[0].text=u"Renamed";CHECK(w::menu_update(menu.items,changed.items)==w::MenuUpdate::layout);
    changed.items[0].children.push_back({12,0,0,0,u"New",{}});CHECK(w::menu_update(menu.items,changed.items)==w::MenuUpdate::structure);
    changed=menu;changed.items.clear();CHECK(w::menu_update(menu.items,changed.items)==w::MenuUpdate::structure);checks+=5;
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.render-submission-memory.v1\",\"checks\":"<<checks
        <<",\"compact_screen_cases\":"<<packed_cases<<",\"triangle_order_cases\":"<<triangle_cases<<",\"compact_vertex_bytes\":"<<sizeof(g::ScreenVertex)<<",\"previous_fixed_vertex_bytes\":160"
        <<",\"synthetic_texture_draws\":128,\"synthetic_full_state_applies\":"<<full<<",\"synthetic_texture_only_applies\":"<<textures
        <<",\"alias_sampler_precedence\":true,\"null_texture_shader_invalidation\":true,\"read_only_queries_preserve_batches\":true,\"shader_constant_bits_preserved\":true,\"menu_update_classification\":true,\"frame_metrics\":"<<metrics.report()
        <<",\"multi_texture_batch\":{\"slots\":8,\"source_draws\":"<<texture_batch.source_draws<<",\"recorded_driver_packets\":"<<texture_batch.driver_packets
        <<",\"triangles\":"<<texture_batch.triangles<<",\"single_image_reuse_packets\":"<<texture_batch.reused_texture_packets<<",\"lifetime_checks\":"<<texture_batch.lifetime_checks
        <<",\"unchanged_vertex_bits_uv_colour_and_order\":true,\"indexed_inputs_and_capacity_checks\":true,\"same_production_packet_builder\":true}"
        <<",\"actual_driver_calls_measured\":false,\"whole_game_fps_measured\":false,\"graphics_device_created\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";
    std::cout<<"PASS submission cache, alias samplers, constant bytes, bounded frame counters and incremental menu classification in memory.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
