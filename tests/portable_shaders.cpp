#include "fixed_pipeline.hpp"
#include "shader_source.hpp"
#include <mojoshader.h>
#include <mojoshader_assembler.h>
#include <cmath>
#include <fstream>
#include <iostream>
#include <memory>
#include <filesystem>
#include <set>
#include <vector>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
namespace g=wa2::graphics;using namespace g::abi;
struct Free {void operator()(const MOJOSHADER_parseData* data)const{if(data)MOJOSHADER_freeParseData(data);}};
using Parsed=std::unique_ptr<const MOJOSHADER_parseData,Free>;
std::filesystem::path source_directory;
struct ShaderSource {std::string file,profile;bool pixel;};
std::vector<ShaderSource> shader_sources;
void errors(const MOJOSHADER_parseData* data,const std::string& input){
    if(!data)throw std::runtime_error("no shader parse result");if(data->error_count){std::string message;for(int n=0;n<data->error_count;++n)message+=std::string(data->errors[n].error)+"\n";throw std::runtime_error(message+input);}
}
unsigned check_shader(const std::string& assembly,bool pixel,std::set<std::pair<int,int>> expected_inputs={}){
    Parsed bytecode(MOJOSHADER_assemble("generated-pipeline",assembly.data(),unsigned(assembly.size()),nullptr,0,nullptr,0,nullptr,0,nullptr,nullptr,nullptr,nullptr,nullptr));errors(bytecode.get(),assembly);
    for(const auto* profile:{MOJOSHADER_PROFILE_GLSL120,MOJOSHADER_PROFILE_GLSLES3}){
        Parsed translated(MOJOSHADER_parse(profile,"main",reinterpret_cast<const unsigned char*>(bytecode->output),bytecode->output_len,nullptr,0,nullptr,0,nullptr,nullptr,nullptr));errors(translated.get(),assembly);
        const std::string source(translated->output,translated->output_len);const auto main=source.find("void main()");CHECK(main!=source.npos);
        std::set<std::pair<int,int>> inputs;
        for(int n=0;n<translated->attribute_count;++n){const auto& input=translated->attributes[n];CHECK(inputs.emplace(input.usage,input.index).second);
            if(pixel)CHECK(input.usage==MOJOSHADER_USAGE_COLOR || input.usage==MOJOSHADER_USAGE_TEXCOORD);}
        if(!expected_inputs.empty())CHECK(inputs==expected_inputs);
        if(pixel && translated->major_ver==1 && translated->minor_ver<4){
            for(const auto& [usage,index]:inputs)if(usage==MOJOSHADER_USAGE_TEXCOORD){
                const auto reg="ps_t"+std::to_string(index);
                // Coordinates must be copied once per invocation before the
                // destructive texture op, never aliased to a read-only input.
                CHECK(source.find("#define "+reg+" ")==source.npos);
                CHECK(source.find(reg+" = ")>main);
                if(std::string(profile)==MOJOSHADER_PROFILE_GLSLES3){
                    const auto input="io_5_"+std::to_string(index);
                    CHECK(source.find("in highp vec4 "+input+";")<main);
                    CHECK(source.find(reg+" = "+input+";")>main && source.find(reg+" = "+input+";")!=source.npos);
                }
            }
        }
        if(assembly==g::movie_pixel_assembly()){
            CHECK(translated->sampler_count==3);
            for(int n=0;n<3;++n)CHECK(translated->samplers[n].index==n && translated->samplers[n].type==MOJOSHADER_SAMPLER_2D);
        }
        if(pixel && assembly.find("dcl_texcoord7 v9")!=assembly.npos){
            CHECK(translated->sampler_count==8);
            for(int n=0;n<8;++n)CHECK(translated->samplers[n].index==n && translated->samplers[n].type==MOJOSHADER_SAMPLER_2D);
            CHECK(source.find("textureGrad")!=source.npos || source.find("texture2DGrad")!=source.npos);
            CHECK(source.find("dFdx")!=source.npos && source.find("dFdy")!=source.npos);
        }
        const auto compiled_source=pixel?g::alpha_test_source(source):source;
        if(pixel)CHECK(compiled_source.find("void wa2_original_shader_main()")!=source.npos && compiled_source.find("wa2_alpha_reference")!=source.npos && compiled_source.find("discard;")!=source.npos);
        const auto filename="shader-"+std::to_string(shader_sources.size())+"-"+profile+(pixel?".frag":".vert");
        std::ofstream output(source_directory/filename,std::ios::binary);output<<compiled_source;CHECK(output);shader_sources.push_back({filename,profile,pixel});
    }return 2;
}
int main(int argc,char** argv){try{
    CHECK(argc==3);source_directory=argv[2];std::filesystem::create_directories(source_directory);
    unsigned translations=check_shader(g::fixed_vertex_assembly(),false)+check_shader(g::movie_pixel_assembly(),true,{{MOJOSHADER_USAGE_TEXCOORD,0}}),variants{};
    // Shader model 2 uses implicit colour/texture semantics. Preserve that ABI
    // while retaining the explicit usage/index fields required by model 3.
    translations+=check_shader("ps_2_0\ndcl v0\ndcl t0.xy\ndcl_2d s0\ntexld r0, t0, s0\nmul oC0, r0, v0\n",true,{{MOJOSHADER_USAGE_COLOR,0},{MOJOSHADER_USAGE_TEXCOORD,0}});
    // Model 1 texture registers start with interpolated coordinates, then
    // TEX overwrites them. The input interface itself must remain read-only.
    unsigned legacy_texture_cases{};
    for(const std::string version:{"ps_1_1","ps_1_2","ps_1_3"}){
        for(unsigned unit=0;unit<4;++unit)for(bool sample:{false,true}){
            const auto reg="t"+std::to_string(unit);
            translations+=check_shader(version+"\n"+(sample?"tex "+reg+"\n":"")+"mov r0, "+reg+"\n",true,
                {{MOJOSHADER_USAGE_TEXCOORD,int(unit)}});++legacy_texture_cases;
        }
        translations+=check_shader(version+"\ntex t0\ntex t1\nmul r0, t0, t1\n",true,
            {{MOJOSHADER_USAGE_TEXCOORD,0},{MOJOSHADER_USAGE_TEXCOORD,1}});++legacy_texture_cases;
        translations+=check_shader(version+"\nmul r0, t0, t1\n",true,
            {{MOJOSHADER_USAGE_TEXCOORD,0},{MOJOSHADER_USAGE_TEXCOORD,1}});++legacy_texture_cases;
    }
    const unsigned operations[]={D3DTOP_SELECTARG1,D3DTOP_SELECTARG2,D3DTOP_MODULATE,D3DTOP_MODULATE2X,D3DTOP_MODULATE4X,D3DTOP_ADD,D3DTOP_SUBTRACT,D3DTOP_BLENDTEXTUREALPHA};
    for(auto operation:operations)for(unsigned unit:{0u,7u})for(unsigned modifiers:{0u,D3DTA_COMPLEMENT,D3DTA_ALPHAREPLICATE,D3DTA_COMPLEMENT|D3DTA_ALPHAREPLICATE})for(bool projected:{false,true}){
        g::FixedPipeline pipeline;for(auto& stage:pipeline.stages)stage={D3DTOP_SELECTARG1,D3DTA_CURRENT,0,D3DTOP_SELECTARG1,D3DTA_CURRENT,0,0,false};
        pipeline.stages[unit]={operation,D3DTA_TEXTURE|modifiers,D3DTA_CURRENT,operation,D3DTA_TEXTURE,D3DTA_DIFFUSE,projected?0x104u:0,true};
        translations+=check_shader(g::fixed_pixel_assembly(pipeline),true);++variants;
    }
    unsigned texture_batch_variants{};
    for(auto operation:operations)for(unsigned modifiers:{0u,D3DTA_COMPLEMENT,D3DTA_ALPHAREPLICATE,D3DTA_COMPLEMENT|D3DTA_ALPHAREPLICATE}){
        g::FixedPipeline pipeline{};pipeline.stages[0]={operation,D3DTA_TEXTURE|modifiers,D3DTA_DIFFUSE,
            D3DTOP_MODULATE,D3DTA_TEXTURE,D3DTA_DIFFUSE,0,true};pipeline.stages[1].colour_op=D3DTOP_DISABLE;
        CHECK(g::fixed_pipeline_sample_mask(pipeline)==1);
        translations+=check_shader(g::fixed_pixel_assembly(pipeline,8),true,
            {{MOJOSHADER_USAGE_COLOR,0},{MOJOSHADER_USAGE_TEXCOORD,0},{MOJOSHADER_USAGE_TEXCOORD,7}});++texture_batch_variants;
    }
    g::TextureStage stage{D3DTOP_MODULATE,D3DTA_TEXTURE,D3DTA_DIFFUSE,D3DTOP_MODULATE,D3DTA_TEXTURE,D3DTA_DIFFUSE,0,true};
    const auto result=g::fixed_stage_reference(stage,{1,1,1,1},{.5f,.25f,.75f,.5f},{.25f,.5f,1,.5f},{1,1,1,1});CHECK((result==std::array<float,4>{.125f,.125f,.75f,.25f}));
    stage.colour_op=D3DTOP_SELECTARG1;stage.colour_arg1=D3DTA_TEXTURE|D3DTA_COMPLEMENT|D3DTA_ALPHAREPLICATE;
    const auto complemented=g::fixed_stage_reference(stage,{1,1,1,1},{1,1,1,1},{.1f,.2f,.3f,.25f},{1,1,1,1});CHECK(complemented[0]==.75f && complemented[1]==.75f && complemented[2]==.75f);
    // A solid TFactor stage is valid without a texture. A null TEXTURE arg1
    // instead terminates the whole cascade, including later bound stages.
    g::FixedPipeline solid;solid.stages[0]={D3DTOP_SELECTARG1,D3DTA_TFACTOR,0xffffffffu,D3DTOP_SELECTARG1,D3DTA_TFACTOR,0xffffffffu,0,false};solid.stages[1].colour_op=D3DTOP_DISABLE;
    const auto solid_source=g::fixed_pixel_assembly(solid);CHECK(solid_source.find("texld")==solid_source.npos);translations+=check_shader(solid_source,true);
    CHECK((g::fixed_stage_reference(solid.stages[0],{1,1,1,1},{1,1,1,1},{0,0,0,0},{.2f,.3f,.4f,.5f})==std::array<float,4>{.2f,.3f,.4f,.5f}));
    solid.stages[0].colour_arg1=D3DTA_TEXTURE;solid.stages[1]=stage;const auto terminated=g::fixed_pixel_assembly(solid);CHECK(terminated.find("texld")==terminated.npos);translations+=check_shader(terminated,true);
    unsigned movie_vectors{};
    for(bool full_range:{false,true})for(bool bt709:{false,true})for(bool white:{false,true}){
        const auto c=g::movie_colour_constants(full_range,bt709);const float y=(white?(full_range?255.f:235.f):(full_range?0.f:16.f))/255+c[0][0];
        const auto grey=y*c[1][0];CHECK(std::abs(grey-(white?1.f:0.f))<0.00001f);CHECK(c[0][1]==-128.f/255 && c[0][2]==-128.f/255 && c[2][1]==1);++movie_vectors;
    }
    const auto position=g::screen_position({10,20,.25f,.5f},D3DVIEWPORT9{10,20,100,100,0,1});CHECK(std::abs(position[0]+1.98f)<0.000001f && position[2]==-1 && position[3]==2);
    bool rejected=false;try{g::alpha_test_source("void main(){}");}catch(const std::invalid_argument&){rejected=true;}CHECK(rejected);
    std::ofstream report(argv[1]);report<<"{\"schema\":\"wa2.portable-shaders.v1\",\"fixed_pipeline_variants\":"<<variants<<",\"desktop_and_gles_translations\":"<<translations<<",\"movie_range_vectors\":"<<movie_vectors<<",\"legacy_ps1_texture_cases\":"<<legacy_texture_cases<<",\"legacy_ps1_mutable_registers_and_input_declarations\":true,\"untextured_solid_and_null_cascade\":true,\"source_abi_layouts\":true,\"fixed_colour_expected_values\":true,\"half_pixel_and_projection\":true,\"unique_input_semantics\":true,\"pixel_colour_and_texcoord_semantics\":true,\"movie_texcoord0_and_yuv_samplers\":true,\"legacy_ps2_implicit_semantics\":true,\"translated_sources\":[";
    for(unsigned n=0;n<shader_sources.size();++n){if(n)report<<',';const auto& s=shader_sources[n];report<<"{\"file\":\""<<s.file<<"\",\"profile\":\""<<s.profile<<"\",\"pixel\":"<<(s.pixel?"true":"false")<<'}';}
    report<<"],\"texture_batch_variants\":"<<texture_batch_variants<<",\"texture_batch_explicit_derivatives\":true,\"graphics_device_created\":false,\"gpu_shader_compile_verified\":false,\"screen_access\":false,\"game_launched\":false,\"passed\":true}\n";
    std::cout<<"PASS portable shader translation: "<<variants<<" pipeline variants for desktop GL and GLES3; no graphics device.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
