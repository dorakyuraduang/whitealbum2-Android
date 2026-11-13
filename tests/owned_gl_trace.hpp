#pragma once
// Recording of this fixture's own submitted CPU buffers and driver state.
// Delegates every GL call unchanged. Never reads pixels, framebuffers or an
// external application's memory. Enabled only by the explicit test argument.
#include "../platform/render/gl_api.hpp"
#include "../platform/render/screen_vertices.hpp"
#include <bit>
#include <fstream>
#include <map>
#include <vector>

#if WA2_RENDERER_COMMAND_TRACE
namespace owned_gl_trace {
inline bool enabled{};
inline GLuint vertex_buffer{},index_buffer{},active_unit{},program{};
inline std::array<GLuint,32> textures{};
inline std::map<GLuint,std::vector<std::uint8_t>> buffers;
inline std::map<GLuint,unsigned> texture_names;
inline unsigned next_texture{};
inline std::map<std::string,std::vector<std::uint8_t>> pipeline;
inline std::map<GLuint,std::map<GLint,std::string>> uniform_names;
inline std::map<GLuint,GLint> position_indices;
inline std::map<GLuint,std::map<std::string,std::vector<std::uint8_t>>> uniforms;
inline std::map<GLuint,std::map<std::string,GLint>> samplers;
inline std::map<GLuint,std::map<GLenum,std::vector<std::uint8_t>>> parameters;
inline std::vector<std::uint8_t> commands;
inline unsigned draw_calls{};
struct Position {GLuint buffer{};GLsizei stride{};std::size_t offset{};};
inline Position position;
template<class T,std::size_t Extent>std::vector<std::uint8_t> bytes(std::span<T,Extent> value){
    const auto* p=reinterpret_cast<const std::uint8_t*>(value.data());return {p,p+value.size_bytes()};
}
inline void state(const char* name,std::initializer_list<std::uint64_t> words){pipeline[name]=bytes(std::span(words.begin(),words.size()));}
inline void append(const void* p,std::size_t size){const auto* b=static_cast<const std::uint8_t*>(p);commands.insert(commands.end(),b,b+size);}
inline void hash(std::uint64_t& h,const void* data,std::size_t size){for(auto b:std::span(static_cast<const std::uint8_t*>(data),size)){h^=b;h*=1099511628211ull;}}
template<class Map>void hash_map(std::uint64_t& h,const Map& values){for(const auto& [key,value]:values){
    if constexpr(std::is_same_v<std::decay_t<decltype(key)>,std::string>)hash(h,key.data(),key.size());else hash(h,&key,sizeof(key));
    hash(h,value.data(),value.size());}}
inline void uniform(GLint location,const void* data,std::size_t size){
    if(location<0)return;const auto name=uniform_names[program].at(location);
    if(name.starts_with("ps_s") || name.starts_with("vs_s")){if(size==sizeof(GLint))std::memcpy(&samplers[program][name],data,size);return;}
    const auto* b=static_cast<const std::uint8_t*>(data);uniforms[program][name]={b,b+size};
}
inline void draw(GLenum mode,GLsizei count,GLenum type,const void* indices,bool indexed){
    if(!position.buffer || position.stride<36 || position.stride>512)throw std::runtime_error("unsupported owned trace vertex shape");
    std::vector<unsigned> order;
    if(mode==GL_TRIANGLES){for(unsigned n=0;n<unsigned(count);++n)order.push_back(n);}
    else if(mode==GL_TRIANGLE_STRIP){for(unsigned n=2;n<unsigned(count);++n){if(n&1)order.insert(order.end(),{n-1,n-2,n});else order.insert(order.end(),{n-2,n-1,n});}}
    else if(mode==GL_TRIANGLE_FAN){for(unsigned n=2;n<unsigned(count);++n)order.insert(order.end(),{0,n-1,n});}
    else throw std::runtime_error("unsupported owned trace primitive");
    std::uint64_t state_hash=1469598103934665603ull;hash_map(state_hash,pipeline);hash_map(state_hash,uniforms[program]);hash(state_hash,&program,sizeof(program));
    const auto& vertices=buffers.at(position.buffer);const auto& elements=buffers[index_buffer];
    const auto index_offset=reinterpret_cast<std::uintptr_t>(indices);
    for(unsigned n:order){unsigned at=n;
        if(indexed){const auto width=type==GL_UNSIGNED_SHORT?2u:type==GL_UNSIGNED_INT?4u:0u;
            if(!width || index_offset+std::size_t(n+1)*width>elements.size())throw std::runtime_error("owned trace index range");
            at=0;std::memcpy(&at,elements.data()+index_offset+n*width,width);}
        const auto start=position.offset+std::size_t(at)*position.stride;
        if(start+position.stride>vertices.size())throw std::runtime_error("owned trace vertex range");
        unsigned binding{};if(position.stride==40){float raw;std::memcpy(&raw,vertices.data()+start+36,4);
            if(raw<0 || raw>15 || raw!=float(unsigned(raw)))throw std::runtime_error("owned trace texture selector");binding=unsigned(raw);}
        unsigned unit=binding;const auto sampler=samplers[program].find("ps_s"+std::to_string(binding));
        if(sampler!=samplers[program].end())unit=unsigned(sampler->second);
        if(unit>=textures.size())throw std::runtime_error("owned trace sampler unit");
        const auto texture=textures[unit];const auto name=texture?texture_names.at(texture):0u;
        auto effective=state_hash;hash_map(effective,parameters[texture]);
        const unsigned payload=position.stride==40?36:unsigned(position.stride);
        append(&payload,sizeof(payload));append(vertices.data()+start,payload);append(&name,sizeof(name));append(&effective,sizeof(effective));
    }
    ++draw_calls;
}
#define OWN_GL(name,type,params,args,record) \
 inline type original_##name{};inline void APIENTRY trace_##name params {if(enabled){record;}original_##name args;}
OWN_GL(BindBuffer,PFNGLBINDBUFFERPROC,(GLenum target,GLuint value),(target,value),if(target==GL_ARRAY_BUFFER)vertex_buffer=value;else if(target==GL_ELEMENT_ARRAY_BUFFER)index_buffer=value)
OWN_GL(BufferData,PFNGLBUFFERDATAPROC,(GLenum target,GLsizeiptr size,const void* data,GLenum usage),(target,size,data,usage),
 auto& out=buffers[target==GL_ARRAY_BUFFER?vertex_buffer:index_buffer];out.assign(std::size_t(size),0);if(data)std::memcpy(out.data(),data,std::size_t(size)))
OWN_GL(BufferSubData,PFNGLBUFFERSUBDATAPROC,(GLenum target,GLintptr offset,GLsizeiptr size,const void* data),(target,offset,size,data),
 auto& out=buffers.at(target==GL_ARRAY_BUFFER?vertex_buffer:index_buffer);if(offset<0 || size<0 || std::size_t(offset)+std::size_t(size)>out.size())throw std::runtime_error("owned trace upload range");std::memcpy(out.data()+offset,data,std::size_t(size)))
OWN_GL(VertexAttribPointer,PFNGLVERTEXATTRIBPOINTERPROC,(GLuint index,GLint size,GLenum type,GLboolean normalized,GLsizei stride,const void* pointer),(index,size,type,normalized,stride,pointer),
 if(size==4 && type==GL_FLOAT && (position_indices.contains(program)?position_indices[program]==GLint(index):(stride==36 || stride==40)))position=(Position{vertex_buffer,stride,reinterpret_cast<std::uintptr_t>(pointer)}))
OWN_GL(DrawElements,decltype(&::glDrawElements),(GLenum mode,GLsizei count,GLenum type,const void* indices),(mode,count,type,indices),draw(mode,count,type,indices,true))
OWN_GL(DrawArrays,decltype(&::glDrawArrays),(GLenum mode,GLint first,GLsizei count),(mode,first,count),if(first)throw std::runtime_error("owned trace first vertex");draw(mode,count,0,nullptr,false))
OWN_GL(ActiveTexture,PFNGLACTIVETEXTUREPROC,(GLenum value),(value),active_unit=value-GL_TEXTURE0)
OWN_GL(BindTexture,decltype(&::glBindTexture),(GLenum target,GLuint value),(target,value),if(target==GL_TEXTURE_2D){if(active_unit>=textures.size())throw std::runtime_error("owned trace texture unit");textures[active_unit]=value;})
inline decltype(&::glGenTextures) original_GenTextures{};
inline void APIENTRY trace_GenTextures(GLsizei count,GLuint* values){original_GenTextures(count,values);if(enabled)for(int n=0;n<count;++n)texture_names[values[n]]=++next_texture;}
OWN_GL(Viewport,decltype(&::glViewport),(GLint x,GLint y,GLsizei w,GLsizei h),(x,y,w,h),state("viewport",{unsigned(x),unsigned(y),unsigned(w),unsigned(h)}))
OWN_GL(BindFramebuffer,PFNGLBINDFRAMEBUFFERPROC,(GLenum target,GLuint value),(target,value),if(target==GL_FRAMEBUFFER)state("framebuffer",{value}))
OWN_GL(Enable,decltype(&::glEnable),(GLenum value),(value),state(("enabled:"+std::to_string(value)).c_str(),{1}))
OWN_GL(Disable,decltype(&::glDisable),(GLenum value),(value),state(("enabled:"+std::to_string(value)).c_str(),{0}))
OWN_GL(BlendFuncSeparate,PFNGLBLENDFUNCSEPARATEPROC,(GLenum a,GLenum b,GLenum c,GLenum d),(a,b,c,d),state("blend",{a,b,c,d}))
OWN_GL(BlendEquationSeparate,PFNGLBLENDEQUATIONSEPARATEPROC,(GLenum a,GLenum b),(a,b),state("equation",{a,b}))
OWN_GL(BlendColor,PFNGLBLENDCOLORPROC,(GLfloat a,GLfloat b,GLfloat c,GLfloat d),(a,b,c,d),state("blend_color",{std::bit_cast<unsigned>(a),std::bit_cast<unsigned>(b),std::bit_cast<unsigned>(c),std::bit_cast<unsigned>(d)}))
OWN_GL(DepthMask,decltype(&::glDepthMask),(GLboolean value),(value),state("depth_mask",{value}))
OWN_GL(DepthFunc,decltype(&::glDepthFunc),(GLenum value),(value),state("depth_func",{value}))
OWN_GL(ColorMask,decltype(&::glColorMask),(GLboolean a,GLboolean b,GLboolean c,GLboolean d),(a,b,c,d),state("color_mask",{a,b,c,d}))
OWN_GL(CullFace,decltype(&::glCullFace),(GLenum value),(value),state("cull",{value}))
OWN_GL(FrontFace,decltype(&::glFrontFace),(GLenum value),(value),state("front",{value}))
OWN_GL(Scissor,decltype(&::glScissor),(GLint a,GLint b,GLsizei c,GLsizei d),(a,b,c,d),state("scissor",{unsigned(a),unsigned(b),unsigned(c),unsigned(d)}))
OWN_GL(UseProgram,PFNGLUSEPROGRAMPROC,(GLuint value),(value),program=value)
OWN_GL(TexParameteri,decltype(&::glTexParameteri),(GLenum target,GLenum key,GLint value),(target,key,value),if(target==GL_TEXTURE_2D)parameters[textures[active_unit]][key]=bytes(std::span(&value,1)))
OWN_GL(TexParameterfv,decltype(&::glTexParameterfv),(GLenum target,GLenum key,const GLfloat* value),(target,key,value),if(target==GL_TEXTURE_2D)parameters[textures[active_unit]][key]=bytes(std::span(value,4)))
inline PFNGLGETUNIFORMLOCATIONPROC original_GetUniformLocation{};
inline GLint APIENTRY trace_GetUniformLocation(GLuint owner,const GLchar* name){const auto at=original_GetUniformLocation(owner,name);if(enabled && at>=0)uniform_names[owner][at]=name;return at;}
inline PFNGLGETATTRIBLOCATIONPROC original_GetAttribLocation{};
inline GLint APIENTRY trace_GetAttribLocation(GLuint owner,const GLchar* name){const auto at=original_GetAttribLocation(owner,name);if(enabled && std::string_view(name)=="vs_v0" && at>=0)position_indices[owner]=at;return at;}
OWN_GL(Uniform1i,PFNGLUNIFORM1IPROC,(GLint at,GLint value),(at,value),uniform(at,&value,sizeof(value)))
OWN_GL(Uniform1f,PFNGLUNIFORM1FPROC,(GLint at,GLfloat value),(at,value),uniform(at,&value,sizeof(value)))
OWN_GL(Uniform4fv,PFNGLUNIFORM4FVPROC,(GLint at,GLsizei count,const GLfloat* value),(at,count,value),uniform(at,value,std::size_t(count)*16))
#undef OWN_GL
inline void finish(const std::string& path){if(!enabled)return;std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(commands.data()),std::streamsize(commands.size()));if(!out)throw std::runtime_error("owned trace output");}
}
extern "C" void* __real_SDL_GL_GetProcAddress(const char*);
extern "C" void* SDL_GL_GetProcAddress(const char* name){
    void* result=__real_SDL_GL_GetProcAddress(name);if(!result)return nullptr;using namespace owned_gl_trace;
#define OWN_GL_HOOK(value) if(std::string_view(name)=="gl" #value){original_##value=reinterpret_cast<decltype(original_##value)>(result);return reinterpret_cast<void*>(trace_##value);}
    OWN_GL_HOOK(BindBuffer) OWN_GL_HOOK(BufferData) OWN_GL_HOOK(BufferSubData) OWN_GL_HOOK(VertexAttribPointer)
    OWN_GL_HOOK(DrawElements) OWN_GL_HOOK(DrawArrays) OWN_GL_HOOK(ActiveTexture) OWN_GL_HOOK(BindTexture) OWN_GL_HOOK(GenTextures)
    OWN_GL_HOOK(Viewport) OWN_GL_HOOK(BindFramebuffer) OWN_GL_HOOK(Enable) OWN_GL_HOOK(Disable)
    OWN_GL_HOOK(BlendFuncSeparate) OWN_GL_HOOK(BlendEquationSeparate) OWN_GL_HOOK(BlendColor)
    OWN_GL_HOOK(DepthMask) OWN_GL_HOOK(DepthFunc) OWN_GL_HOOK(ColorMask) OWN_GL_HOOK(CullFace) OWN_GL_HOOK(FrontFace) OWN_GL_HOOK(Scissor)
    OWN_GL_HOOK(UseProgram) OWN_GL_HOOK(TexParameteri) OWN_GL_HOOK(TexParameterfv) OWN_GL_HOOK(GetUniformLocation)
    OWN_GL_HOOK(GetAttribLocation)
    OWN_GL_HOOK(Uniform1i) OWN_GL_HOOK(Uniform1f) OWN_GL_HOOK(Uniform4fv)
#undef OWN_GL_HOOK
    return result;
}
#else
namespace owned_gl_trace {inline bool enabled{};inline void finish(const std::string&){};}
#endif
