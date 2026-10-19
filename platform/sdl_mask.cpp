#include "sdl_mask.hpp"
#include <SDL.h>
#define SDL_USE_BUILTIN_OPENGL_DEFINITIONS
#include <SDL_opengles2.h>
#include <array>
#include <stdexcept>
#include <string>

namespace wa2::platform {
struct SdlMaskProgram::Impl {
#define PROC(result,name,...) result (GL_APIENTRY *name)(__VA_ARGS__){}
    PROC(GLuint,glCreateShader,GLenum);
    PROC(void,glShaderSource,GLuint,GLsizei,const GLchar* const*,const GLint*);
    PROC(void,glCompileShader,GLuint);
    PROC(void,glGetShaderiv,GLuint,GLenum,GLint*);
    PROC(void,glGetShaderInfoLog,GLuint,GLsizei,GLsizei*,GLchar*);
    PROC(void,glDeleteShader,GLuint);
    PROC(GLuint,glCreateProgram,void);
    PROC(void,glAttachShader,GLuint,GLuint);
    PROC(void,glBindAttribLocation,GLuint,GLuint,const GLchar*);
    PROC(void,glLinkProgram,GLuint);
    PROC(void,glGetProgramiv,GLuint,GLenum,GLint*);
    PROC(void,glGetProgramInfoLog,GLuint,GLsizei,GLsizei*,GLchar*);
    PROC(void,glDeleteProgram,GLuint);
    PROC(GLint,glGetUniformLocation,GLuint,const GLchar*);
    PROC(void,glUseProgram,GLuint);
    PROC(void,glUniform1i,GLint,GLint);
    PROC(void,glUniform1f,GLint,GLfloat);
    PROC(void,glUniform3f,GLint,GLfloat,GLfloat,GLfloat);
    PROC(void,glGenBuffers,GLsizei,GLuint*);
    PROC(void,glBindBuffer,GLenum,GLuint);
    PROC(void,glBufferData,GLenum,GLsizeiptr,const void*,GLenum);
    PROC(void,glDeleteBuffers,GLsizei,const GLuint*);
    PROC(void,glGetIntegerv,GLenum,GLint*);
    PROC(void,glGetVertexAttribiv,GLuint,GLenum,GLint*);
    PROC(void,glGetVertexAttribPointerv,GLuint,GLenum,void**);
    PROC(void,glEnableVertexAttribArray,GLuint);
    PROC(void,glDisableVertexAttribArray,GLuint);
    PROC(void,glVertexAttribPointer,GLuint,GLint,GLenum,GLboolean,GLsizei,const void*);
    PROC(void,glActiveTexture,GLenum);
    PROC(void,glBindTexture,GLenum,GLuint);
    PROC(GLboolean,glIsEnabled,GLenum);
    PROC(void,glEnable,GLenum);
    PROC(void,glDisable,GLenum);
    PROC(void,glBlendEquationSeparate,GLenum,GLenum);
    PROC(void,glBlendFuncSeparate,GLenum,GLenum,GLenum,GLenum);
    PROC(void,glDrawArrays,GLenum,GLint,GLsizei);
#undef PROC
    SDL_GLContext context{};
    GLuint program{},buffer{};
    GLint image_uniform{},mask_uniform{},bias_uniform{},gain_uniform{},flags_uniform{},target_uniform{};

    Impl(){
#define LOAD(name) name=reinterpret_cast<decltype(name)>(SDL_GL_GetProcAddress(#name));if(!name)throw std::runtime_error("Missing GLES2 function " #name)
        LOAD(glCreateShader);LOAD(glShaderSource);LOAD(glCompileShader);LOAD(glGetShaderiv);LOAD(glGetShaderInfoLog);LOAD(glDeleteShader);
        LOAD(glCreateProgram);LOAD(glAttachShader);LOAD(glBindAttribLocation);LOAD(glLinkProgram);LOAD(glGetProgramiv);LOAD(glGetProgramInfoLog);
        LOAD(glDeleteProgram);LOAD(glGetUniformLocation);LOAD(glUseProgram);LOAD(glUniform1i);LOAD(glUniform1f);LOAD(glUniform3f);
        LOAD(glGenBuffers);LOAD(glBindBuffer);LOAD(glBufferData);LOAD(glDeleteBuffers);LOAD(glGetIntegerv);LOAD(glGetVertexAttribiv);
        LOAD(glGetVertexAttribPointerv);LOAD(glEnableVertexAttribArray);LOAD(glDisableVertexAttribArray);LOAD(glVertexAttribPointer);
        LOAD(glActiveTexture);LOAD(glBindTexture);LOAD(glIsEnabled);LOAD(glEnable);LOAD(glDisable);LOAD(glBlendEquationSeparate);
        LOAD(glBlendFuncSeparate);LOAD(glDrawArrays);
#undef LOAD
        context=SDL_GL_GetCurrentContext();if(!context)throw std::runtime_error("Native mask shader requires a GLES2 context");
        constexpr const char* vertex=
            "attribute vec2 position;attribute vec2 texcoord;varying vec2 uv;uniform float target;"
            "void main(){uv=texcoord;gl_Position=vec4(position.x,position.y*target,0.0,1.0);}";
        constexpr const char* fragment=
            "precision highp float;varying vec2 uv;uniform sampler2D image;uniform sampler2D mask;"
            "uniform float bias;uniform float gain;uniform vec3 flags;"
            "void main(){vec2 m=mix(uv,vec2(1.0)-uv,flags.xy);float a=texture2D(mask,m).a;"
            "a=mix(a,1.0-a,flags.z);vec4 colour=texture2D(image,uv);"
            "colour.a=clamp((a+bias)*gain,0.0,1.0);gl_FragColor=colour;}";
        GLuint vs{},fs{};
        try{
            vs=shader(GL_VERTEX_SHADER,vertex);fs=shader(GL_FRAGMENT_SHADER,fragment);
            program=glCreateProgram();if(!program)throw std::runtime_error("Cannot allocate native mask program");
            glAttachShader(program,vs);glAttachShader(program,fs);glBindAttribLocation(program,0,"position");glBindAttribLocation(program,1,"texcoord");
            glLinkProgram(program);GLint okay{};glGetProgramiv(program,GL_LINK_STATUS,&okay);
            if(!okay){std::array<char,2048> log{};glGetProgramInfoLog(program,GLsizei(log.size()),nullptr,log.data());
                throw std::runtime_error("Native mask program link failed: "+std::string(log.data()));}
            image_uniform=uniform("image");mask_uniform=uniform("mask");bias_uniform=uniform("bias");gain_uniform=uniform("gain");
            flags_uniform=uniform("flags");target_uniform=uniform("target");
            GLint previous{};glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&previous);glGenBuffers(1,&buffer);
            if(!buffer)throw std::runtime_error("Cannot allocate native mask vertices");
            constexpr GLfloat vertices[]{-1,1,0,0,-1,-1,0,1,1,1,1,0,1,-1,1,1};
            glBindBuffer(GL_ARRAY_BUFFER,buffer);glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STATIC_DRAW);
            glBindBuffer(GL_ARRAY_BUFFER,GLuint(previous));
        }catch(...){if(vs)glDeleteShader(vs);if(fs)glDeleteShader(fs);clear();throw;}
        glDeleteShader(vs);glDeleteShader(fs);
    }
    GLuint shader(GLenum type,const char* source){
        auto result=glCreateShader(type);if(!result)throw std::runtime_error("Cannot allocate native mask shader");
        glShaderSource(result,1,&source,nullptr);glCompileShader(result);GLint okay{};glGetShaderiv(result,GL_COMPILE_STATUS,&okay);
        if(!okay){std::array<char,2048> log{};glGetShaderInfoLog(result,GLsizei(log.size()),nullptr,log.data());glDeleteShader(result);
            throw std::runtime_error("Native mask shader compile failed: "+std::string(log.data()));}
        return result;
    }
    GLint uniform(const char* name){const auto value=glGetUniformLocation(program,name);
        if(value<0)throw std::runtime_error("Missing native mask uniform "+std::string(name));return value;}
    void clear()noexcept{
        if(SDL_GL_GetCurrentContext()==context){if(buffer)glDeleteBuffers(1,&buffer);if(program)glDeleteProgram(program);}
        buffer=program=0;
    }
    ~Impl(){clear();}
    struct Saved {
        Impl& gl;
        GLint program{},buffer{},active{},textures[2]{},equation[2]{},factors[4]{};
        GLboolean blend{};
        struct Attribute {GLint enabled{},size{},type{},normalized{},stride{},buffer{};void* pointer{};};
        std::array<Attribute,2> attributes;
        explicit Saved(Impl& api):gl(api){
            gl.glGetIntegerv(GL_CURRENT_PROGRAM,&program);gl.glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&buffer);
            gl.glGetIntegerv(GL_ACTIVE_TEXTURE,&active);blend=gl.glIsEnabled(GL_BLEND);
            gl.glGetIntegerv(GL_BLEND_EQUATION_RGB,&equation[0]);gl.glGetIntegerv(GL_BLEND_EQUATION_ALPHA,&equation[1]);
            gl.glGetIntegerv(GL_BLEND_SRC_RGB,&factors[0]);gl.glGetIntegerv(GL_BLEND_DST_RGB,&factors[1]);
            gl.glGetIntegerv(GL_BLEND_SRC_ALPHA,&factors[2]);gl.glGetIntegerv(GL_BLEND_DST_ALPHA,&factors[3]);
            for(unsigned n=0;n<2;++n){gl.glActiveTexture(GL_TEXTURE0+n);gl.glGetIntegerv(GL_TEXTURE_BINDING_2D,&textures[n]);auto& a=attributes[n];
                gl.glGetVertexAttribiv(n,GL_VERTEX_ATTRIB_ARRAY_ENABLED,&a.enabled);gl.glGetVertexAttribiv(n,GL_VERTEX_ATTRIB_ARRAY_SIZE,&a.size);
                gl.glGetVertexAttribiv(n,GL_VERTEX_ATTRIB_ARRAY_TYPE,&a.type);gl.glGetVertexAttribiv(n,GL_VERTEX_ATTRIB_ARRAY_NORMALIZED,&a.normalized);
                gl.glGetVertexAttribiv(n,GL_VERTEX_ATTRIB_ARRAY_STRIDE,&a.stride);gl.glGetVertexAttribiv(n,GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING,&a.buffer);
                gl.glGetVertexAttribPointerv(n,GL_VERTEX_ATTRIB_ARRAY_POINTER,&a.pointer);}
        }
        ~Saved(){
            for(unsigned n=0;n<2;++n){gl.glActiveTexture(GL_TEXTURE0+n);gl.glBindTexture(GL_TEXTURE_2D,GLuint(textures[n]));const auto& a=attributes[n];
                gl.glBindBuffer(GL_ARRAY_BUFFER,GLuint(a.buffer));gl.glVertexAttribPointer(n,a.size,GLenum(a.type),GLboolean(a.normalized),a.stride,a.pointer);
                if(a.enabled)gl.glEnableVertexAttribArray(n);else gl.glDisableVertexAttribArray(n);}
            gl.glActiveTexture(GLenum(active));gl.glBindBuffer(GL_ARRAY_BUFFER,GLuint(buffer));gl.glUseProgram(GLuint(program));
            gl.glBlendEquationSeparate(GLenum(equation[0]),GLenum(equation[1]));
            gl.glBlendFuncSeparate(GLenum(factors[0]),GLenum(factors[1]),GLenum(factors[2]),GLenum(factors[3]));
            if(blend)gl.glEnable(GL_BLEND);else gl.glDisable(GL_BLEND);
        }
    };
    void draw(SDL_Renderer* renderer,SDL_Texture* image,SDL_Texture* mask,float bias,float gain,bool flip_x,bool flip_y,bool invert){
        if(SDL_GL_GetCurrentContext()!=context)throw std::runtime_error("Native mask program belongs to another GLES2 context");
        Saved saved(*this);
        const bool target=SDL_GetRenderTarget(renderer)!=nullptr;
        struct Unbind {
            Impl& gl;SDL_Texture* image;SDL_Texture* mask;
            ~Unbind(){gl.glActiveTexture(GL_TEXTURE1);SDL_GL_UnbindTexture(mask);gl.glActiveTexture(GL_TEXTURE0);SDL_GL_UnbindTexture(image);}
        } unbind{*this,image,mask};
        glActiveTexture(GL_TEXTURE0);if(SDL_GL_BindTexture(image,nullptr,nullptr))throw std::runtime_error(SDL_GetError());
        glActiveTexture(GL_TEXTURE1);if(SDL_GL_BindTexture(mask,nullptr,nullptr))throw std::runtime_error(SDL_GetError());
        glUseProgram(program);glUniform1i(image_uniform,0);glUniform1i(mask_uniform,1);glUniform1f(bias_uniform,bias);glUniform1f(gain_uniform,gain);
        glUniform3f(flags_uniform,flip_x?1.f:0.f,flip_y?1.f:0.f,invert?1.f:0.f);glUniform1f(target_uniform,target?-1.f:1.f);
        glBindBuffer(GL_ARRAY_BUFFER,buffer);glEnableVertexAttribArray(0);glEnableVertexAttribArray(1);
        glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,4*sizeof(GLfloat),nullptr);
        glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,4*sizeof(GLfloat),reinterpret_cast<const void*>(2*sizeof(GLfloat)));
        glEnable(GL_BLEND);glBlendEquationSeparate(GL_FUNC_ADD,GL_FUNC_ADD);
        glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
        glDrawArrays(GL_TRIANGLE_STRIP,0,4);
    }
};
SdlMaskProgram::SdlMaskProgram():impl_(std::make_unique<Impl>()){}
SdlMaskProgram::~SdlMaskProgram()=default;
void SdlMaskProgram::draw(SDL_Renderer* renderer,SDL_Texture* image,SDL_Texture* mask,float bias,float gain,bool x,bool y,bool invert){
    impl_->draw(renderer,image,mask,bias,gain,x,y,invert);
}
void SdlMaskProgram::abandon()noexcept{impl_->program=impl_->buffer=0;}
}
