#pragma once
#include "../platform/render/texture_batch.hpp"
#include <map>
#include <string>
#include <tuple>
// One cached static sprite-function call and the production packet builder.
// No window, GL context, resource file or game/frame loop is used.
struct PortraitSubmission {
    using U32=std::uint32_t;
    bool active{};
    unsigned draws{},accepted{},packets{},fvf{},vertex_shader{},pixel_shader{};
    std::array<U32,16> textures{};
    std::array<std::array<U32,33>,8> stages{};
    std::map<std::tuple<std::string,U32,U32>,std::vector<U32>> words;
    wa2::graphics::ScreenTextureBatch<U32> batch;
    struct Vertex {U32 texture;wa2::graphics::ScreenVertex value;};
    std::vector<Vertex> original,submitted;
    PortraitSubmission(){using namespace wa2::graphics::abi;
        for(unsigned n=0;n<8;++n){auto& t=stages[n];t[D3DTSS_COLOROP]=n?D3DTOP_DISABLE:D3DTOP_MODULATE;
            t[D3DTSS_COLORARG1]=D3DTA_TEXTURE;t[D3DTSS_COLORARG2]=D3DTA_CURRENT;
            t[D3DTSS_ALPHAOP]=n?D3DTOP_DISABLE:D3DTOP_SELECTARG1;
            t[D3DTSS_ALPHAARG1]=D3DTA_TEXTURE;t[D3DTSS_ALPHAARG2]=D3DTA_CURRENT;t[D3DTSS_TEXCOORDINDEX]=n;}
    }
    void flush(){if(batch.empty())return;++packets;
        for(auto index:batch.indices()){const auto& vertex=batch.vertices()[index];
            submitted.push_back({batch.bindings()[unsigned(vertex.binding)],vertex.screen});}
        batch.clear();
    }
    void method(const std::string& name,std::span<const U32> args){
        if(!active || !name.starts_with("Set"))return;
        if(name=="SetTexture"){CHECK(args[1]<textures.size());textures[args[1]]=args[2];return;}
        U32 group{},item{};
        if(name=="SetTextureStageState" || name=="SetSamplerState"){group=args[1];item=args[2];}
        else if(name=="SetRenderState" || name=="SetRenderTarget")group=args[1];
        else CHECK(name=="SetFVF" || name=="SetVertexShader" || name=="SetPixelShader");
        auto& previous=words[{name,group,item}];const std::vector<U32> value(args.begin()+1,args.end());
        if(previous!=value){flush();previous=value;}
        if(name=="SetFVF")fvf=args[1];
        if(name=="SetVertexShader")vertex_shader=args[1];
        if(name=="SetPixelShader")pixel_shader=args[1];
        if(name=="SetTextureStageState"){CHECK(group<8 && item<33);stages[group][item]=args[3];}
    }
    void draw(U32 type,U32 count,std::span<const std::uint8_t> bytes,U32 stride){
        if(!active)return;using namespace wa2::graphics::abi;namespace g=wa2::graphics;
        ++draws;g::FixedPipeline pipeline{};
        for(unsigned n=0;n<8;++n){const auto& t=stages[n];pipeline.stages[n]={t[D3DTSS_COLOROP],t[D3DTSS_COLORARG1],t[D3DTSS_COLORARG2],
            t[D3DTSS_ALPHAOP],t[D3DTSS_ALPHAARG1],t[D3DTSS_ALPHAARG2],t[D3DTSS_TEXTURETRANSFORMFLAGS],bool(textures[n])};}
        const auto plan=g::screen_vertex_plan(fvf,{0,0,1280,720,0,1},{stages[0][D3DTSS_TEXCOORDINDEX],stages[1][D3DTSS_TEXCOORDINDEX]});
        if(!plan || vertex_shader || pixel_shader || !g::fixed_pipeline_texture_batch(pipeline,g::texture_mask(textures))){
            std::cerr<<"portrait source FVF="<<std::hex<<fvf<<" mask="<<g::texture_mask(textures)<<std::dec
                <<" plan="<<bool(plan)<<" shader="<<vertex_shader<<','<<pixel_shader<<" pipeline_mask="<<g::fixed_pipeline_sample_mask(pipeline)<<'\n';
            for(unsigned n=0;n<2;++n)std::cerr<<"stage "<<n<<" color="<<stages[n][D3DTSS_COLOROP]<<" args="<<stages[n][D3DTSS_COLORARG1]<<','<<stages[n][D3DTSS_COLORARG2]
                <<" alpha="<<stages[n][D3DTSS_ALPHAOP]<<" tex="<<textures[n]<<'\n';
            CHECK(false);
        }
        std::vector<g::ScreenVertex> vertices(bytes.size()/stride);CHECK(g::pack_screen_vertices(*plan,bytes,stride,vertices));
        g::each_triangle(type,count,[&](unsigned a,unsigned b,unsigned c){for(auto index:{a,b,c})original.push_back({textures[0],vertices[index]});});
        if(!batch.fits(textures[0],unsigned(vertices.size()),decltype(batch)::index_count(type,count)))flush();
        CHECK(batch.append(textures[0],vertices,type,count));++accepted;
    }
    void check(unsigned expected_draws=40,unsigned expected_packets=5){flush();CHECK(draws==expected_draws && accepted==expected_draws && packets==expected_packets && original.size()==submitted.size());
        for(unsigned n=0;n<original.size();++n){CHECK(original[n].texture==submitted[n].texture);
            CHECK(!std::memcmp(&original[n].value,&submitted[n].value,sizeof(wa2::graphics::ScreenVertex)));}
    }
};
