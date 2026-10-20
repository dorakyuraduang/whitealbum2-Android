#include "fixed_pipeline.hpp"
#include <algorithm>
#include <stdexcept>
#include <sstream>
namespace wa2::graphics {
namespace {
using namespace abi;
std::string source(std::uint32_t argument,bool alpha){
    if(argument&~(D3DTA_SELECTMASK|D3DTA_COMPLEMENT|D3DTA_ALPHAREPLICATE))throw std::invalid_argument("unsupported texture argument flags");
    std::string value;switch(argument&D3DTA_SELECTMASK){case D3DTA_CURRENT:value="r0";break;case D3DTA_DIFFUSE:value="v0";break;case D3DTA_TEXTURE:value="r1";break;case D3DTA_TFACTOR:value="c0";break;default:throw std::invalid_argument("unsupported fixed texture argument");}
    return value+((alpha || (argument&D3DTA_ALPHAREPLICATE))?".aaaa":"");
}
void operation(std::ostringstream& text,bool alpha,std::uint32_t op,std::uint32_t first,std::uint32_t second){
    if(op==D3DTOP_DISABLE)return;const bool use_first=op!=D3DTOP_SELECTARG2,use_second=op!=D3DTOP_SELECTARG1;
    auto a=use_first?source(first,alpha):"c1.x",b=use_second?source(second,alpha):"c1.x";const auto mask=alpha?".a":".rgb";
    if(use_first && (first&D3DTA_COMPLEMENT)){text<<"sub r2, c1.x, "<<a<<"\n";a="r2";}
    if(use_second && (second&D3DTA_COMPLEMENT)){text<<"sub r3, c1.x, "<<b<<"\n";b="r3";}
    const auto destination=std::string("r0")+mask;
    switch(op){case D3DTOP_SELECTARG1:text<<"mov_sat "<<destination<<", "<<a<<'\n';break;case D3DTOP_SELECTARG2:text<<"mov_sat "<<destination<<", "<<b<<'\n';break;
    case D3DTOP_MODULATE:text<<"mul_sat "<<destination<<", "<<a<<", "<<b<<'\n';break;
    case D3DTOP_MODULATE2X:case D3DTOP_MODULATE4X:text<<"mul r4, "<<a<<", "<<b<<"\nmul_sat "<<destination<<", r4, c1."<<(op==D3DTOP_MODULATE2X?'y':'z')<<'\n';break;
    case D3DTOP_ADD:text<<"add_sat "<<destination<<", "<<a<<", "<<b<<'\n';break;case D3DTOP_SUBTRACT:text<<"sub_sat "<<destination<<", "<<a<<", "<<b<<'\n';break;
    case D3DTOP_BLENDTEXTUREALPHA:text<<"lrp_sat "<<destination<<", r1.a, "<<a<<", "<<b<<'\n';break;
    default:throw std::invalid_argument("unsupported fixed texture operation");}
}
bool needs_texture(std::uint32_t op,std::uint32_t first,std::uint32_t second){
    if(op==D3DTOP_DISABLE)return false;
    return op==D3DTOP_BLENDTEXTUREALPHA || (op!=D3DTOP_SELECTARG2 && (first&D3DTA_SELECTMASK)==D3DTA_TEXTURE) ||
        (op!=D3DTOP_SELECTARG1 && (second&D3DTA_SELECTMASK)==D3DTA_TEXTURE);
}
}
unsigned fixed_pipeline_sample_mask(const FixedPipeline& pipeline){
    unsigned mask{};
    for(unsigned n=0;n<pipeline.stages.size();++n){const auto& stage=pipeline.stages[n];
        if(stage.colour_op==D3DTOP_DISABLE || (!stage.texture && (stage.colour_arg1&D3DTA_SELECTMASK)==D3DTA_TEXTURE))break;
        if(needs_texture(stage.colour_op,stage.colour_arg1,stage.colour_arg2) || needs_texture(stage.alpha_op,stage.alpha_arg1,stage.alpha_arg2)){
            if(!stage.texture)throw std::invalid_argument("null sampled fixed-stage texture");mask|=1u<<n;
        }
    }
    return mask;
}
bool fixed_pipeline_texture_batch(const FixedPipeline& pipeline,std::uint16_t bound_textures){
    if(bound_textures!=1 || pipeline.stages[0].transform_flags)return false;
    try{return fixed_pipeline_sample_mask(pipeline)==1;}catch(const std::invalid_argument&){return false;}
}
std::string fixed_pixel_assembly(const FixedPipeline& pipeline,unsigned texture_slots){
    if(texture_slots!=1 && texture_slots!=8)throw std::invalid_argument("unsupported fixed texture batch size");
    if(texture_slots!=1 && (fixed_pipeline_sample_mask(pipeline)!=1 || pipeline.stages[0].transform_flags))
        throw std::invalid_argument("texture batches require one unprojected logical sampler");
    std::ostringstream declarations,body;declarations<<"ps_3_0\ndcl_color0 v0\ndef c1, 1, 2, 4, 0\n";body<<"mov r0, v0\n";
    if(texture_slots!=1)declarations<<"dcl_texcoord7 v9\ndef c252, 0.5, 1.5, 2.5, 3.5\ndef c253, 4.5, 5.5, 6.5, 7.5\n";
    for(unsigned n=0;n<pipeline.stages.size();++n){const auto& stage=pipeline.stages[n];if(stage.colour_op==abi::D3DTOP_DISABLE)break;
        // The source API terminates the cascade at a null COLORARG1 texture.
        // Untextured diffuse/TFactor stages still execute their operations.
        if(!stage.texture && (stage.colour_arg1&D3DTA_SELECTMASK)==D3DTA_TEXTURE)break;
        const bool sample=needs_texture(stage.colour_op,stage.colour_arg1,stage.colour_arg2) || needs_texture(stage.alpha_op,stage.alpha_arg1,stage.alpha_arg2);
        if(sample){if(!stage.texture)throw std::invalid_argument("null texture used outside the fixed-stage termination contract");
            declarations<<"dcl_texcoord"<<n<<" v"<<n+1<<'\n';
            if(texture_slots!=1){
                for(unsigned slot=0;slot<texture_slots;++slot)declarations<<"dcl_2d s"<<slot<<'\n';
                // Derivatives are evaluated before divergent sampler selection.
                // TEXLDD keeps mip/filter behaviour defined at primitive edges.
                body<<"dsx r6, v1\ndsy r7, v1\n";
                const auto select=[&](auto&& self,unsigned first,unsigned end)->void{
                    if(end-first==1){body<<"texldd r1, v1, s"<<first<<", r6, r7\n";return;}
                    const auto middle=(first+end)/2,threshold=middle-1;
                    body<<"if_lt v9.x, c"<<252+threshold/4<<'.'<<"xyzw"[threshold%4]<<'\n';
                    self(self,first,middle);body<<"else\n";self(self,middle,end);body<<"endif\n";
                };
                select(select,0,texture_slots);
            }else if(stage.transform_flags&0x100){declarations<<"dcl_2d s"<<n<<'\n';const auto count=stage.transform_flags&255;if(count<2 || count>4)throw std::invalid_argument("invalid projected texture coordinate count");
                body<<"rcp r5, v"<<n+1<<'.'<<(count==2?'y':count==3?'z':'w')<<"\nmul r5, v"<<n+1<<", r5\ntexld r1, r5, s"<<n<<'\n';}
            else{declarations<<"dcl_2d s"<<n<<'\n';body<<"texld r1, v"<<n+1<<", s"<<n<<'\n';}}
        operation(body,false,stage.colour_op,stage.colour_arg1,stage.colour_arg2);operation(body,true,stage.alpha_op,stage.alpha_arg1,stage.alpha_arg2);
    }
    body<<"mov oC0, r0\n";return declarations.str()+body.str();
}
std::string subtitle_pixel_assembly(){return
    "ps_2_0\ndcl t0.xy\ndcl_2d s0\ntexld r0, t0, s0\nmov oC0, r0\n";
}
std::string movie_pixel_assembly(){return
    "ps_3_0\ndcl_texcoord0 v0\ndcl_2d s0\ndcl_2d s1\ndcl_2d s2\n"
    "texld r0, v0, s0\ntexld r1, v0, s1\ntexld r2, v0, s2\n"
    "add r0.x, r0.x, c0.x\nadd r1.x, r1.x, c0.y\nadd r2.x, r2.x, c0.z\n"
    "mul r3.xyz, r0.x, c1.x\nmad r3.r, r2.x, c1.y, r3.r\n"
    "mad r3.g, r1.x, c1.z, r3.g\nmad r3.g, r2.x, c1.w, r3.g\n"
    "mad r3.b, r1.x, c2.x, r3.b\nmov r3.a, c2.y\nmov_sat oC0, r3\n";}
std::array<std::array<float,4>,3> movie_colour_constants(bool full_range,bool bt709){return {{
    {full_range?0.f:-16.f/255,-128.f/255,-128.f/255,0},
    full_range?(bt709?std::array<float,4>{1,1.5748f,-.187324f,-.468124f}:std::array<float,4>{1,1.402f,-.344136f,-.714136f}):
        (bt709?std::array<float,4>{1.164384f,1.792741f,-.213249f,-.532909f}:std::array<float,4>{1.164384f,1.596027f,-.391762f,-.812968f}),
    {full_range?(bt709?1.8556f:1.772f):(bt709?2.112402f:2.017232f),1,0,0}}};}
std::string fixed_vertex_assembly(){
    std::ostringstream text;text<<"vs_3_0\ndcl_position0 v0\ndcl_color0 v1\ndcl_position0 o0\ndcl_color0 o1\ndef c254, 0.5, -1, 0, 0\n";
    for(unsigned n=0;n<8;++n)text<<"dcl_texcoord"<<n<<" v"<<n+2<<"\ndcl_texcoord"<<n<<" o"<<n+2<<'\n';
    // Input is already in GL clip coordinates. Undo the translator's normal
    // D3D depth/y mapping for this generated passthrough vertex stage only.
    text<<"mov o0, v0\nmul o0.y, v0.y, c254.y\nadd r0.z, v0.z, v0.w\nmul o0.z, r0.z, c254.x\nmov o1, v1\n";
    for(unsigned n=0;n<8;++n)text<<"mov o"<<n+2<<", v"<<n+2<<'\n';return text.str();
}
std::array<float,4> fixed_stage_reference(const TextureStage& stage,std::array<float,4> current,
    const std::array<float,4>& diffuse,const std::array<float,4>& texture,const std::array<float,4>& factor){
    if(stage.colour_op==D3DTOP_DISABLE || (!stage.texture && (stage.colour_arg1&D3DTA_SELECTMASK)==D3DTA_TEXTURE))return current;
    const auto previous=current;auto argument=[&](std::uint32_t bits,unsigned component){const std::array<float,4>* value{};
        switch(bits&abi::D3DTA_SELECTMASK){case abi::D3DTA_CURRENT:value=&previous;break;case abi::D3DTA_DIFFUSE:value=&diffuse;break;case abi::D3DTA_TEXTURE:value=&texture;break;case abi::D3DTA_TFACTOR:value=&factor;break;default:throw std::invalid_argument("unknown reference texture argument");}
        const auto result=(*value)[bits&abi::D3DTA_ALPHAREPLICATE?3:component];return bits&abi::D3DTA_COMPLEMENT?1-result:result;};
    for(unsigned c=0;c<4;++c){const auto op=c==3?stage.alpha_op:stage.colour_op;if(op==abi::D3DTOP_DISABLE)continue;
        const auto a=op==D3DTOP_SELECTARG2?0:argument(c==3?stage.alpha_arg1:stage.colour_arg1,c),b=op==D3DTOP_SELECTARG1?0:argument(c==3?stage.alpha_arg2:stage.colour_arg2,c);float value{};
        switch(op){case abi::D3DTOP_SELECTARG1:value=a;break;case abi::D3DTOP_SELECTARG2:value=b;break;case abi::D3DTOP_MODULATE:value=a*b;break;
        case abi::D3DTOP_MODULATE2X:value=a*b*2;break;case abi::D3DTOP_MODULATE4X:value=a*b*4;break;case abi::D3DTOP_ADD:value=a+b;break;
        case abi::D3DTOP_SUBTRACT:value=a-b;break;case abi::D3DTOP_BLENDTEXTUREALPHA:value=texture[3]*a+(1-texture[3])*b;break;default:throw std::invalid_argument("unknown reference texture operation");}
        current[c]=std::clamp(value,0.f,1.f);
    }return current;
}
std::array<float,4> screen_position(std::array<float,4> pos,const abi::D3DVIEWPORT9& viewport){
    if(!viewport.Width || !viewport.Height)throw std::invalid_argument("empty source viewport");const float w=pos[3]?1/pos[3]:1;
    return {((pos[0]+.5f-float(viewport.X))*2/float(viewport.Width)-1)*w,
            ((pos[1]+.5f-float(viewport.Y))*2/float(viewport.Height)-1)*w,(2*pos[2]-1)*w,w};
}
}
