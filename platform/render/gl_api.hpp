#pragma once
#include <SDL.h>
// SDL's desktop GL declarations otherwise pull windows.h on Windows. We only
// need the GL calling convention; the transport contains no Win32 types.
#ifndef APIENTRY
#ifdef _WIN32
#define APIENTRY __stdcall
#else
#define APIENTRY
#endif
#endif
#include <SDL_opengl.h>
#include <SDL_opengl_glext.h>
#include <stdexcept>
#include <string>
namespace wa2::graphics {
// All GL entry points come from the owning SDL context, including GL 1.x
// symbols. Thus the same object code links against desktop GL and GLES3.
struct GlApi {
#define WA2_GL_BASE(X) \
 X(GetString) X(GetIntegerv) X(GetError) X(GenTextures) X(DeleteTextures) \
 X(BindTexture) X(TexImage2D) X(TexSubImage2D) X(TexParameteri) X(TexParameterfv) \
 X(PixelStorei) X(ReadPixels) X(Viewport) X(Enable) X(Disable) X(DepthMask) \
 X(DepthFunc) X(CullFace) X(FrontFace) X(Scissor) X(ColorMask) X(StencilMask) \
 X(ClearColor) X(ClearStencil) X(Clear) X(DrawArrays) X(DrawElements)
#define WA2_GL_DECLARE(name) decltype(&::gl##name) name{};
    WA2_GL_BASE(WA2_GL_DECLARE)
#undef WA2_GL_DECLARE
    PFNGLACTIVETEXTUREPROC ActiveTexture{};
    PFNGLBLENDFUNCSEPARATEPROC BlendFuncSeparate{};
    PFNGLBLENDEQUATIONSEPARATEPROC BlendEquationSeparate{};
    PFNGLBLENDCOLORPROC BlendColor{};
    PFNGLGENFRAMEBUFFERSPROC GenFramebuffers{};
    PFNGLDELETEFRAMEBUFFERSPROC DeleteFramebuffers{};
    PFNGLBINDFRAMEBUFFERPROC BindFramebuffer{};
    PFNGLFRAMEBUFFERTEXTURE2DPROC FramebufferTexture2D{};
    PFNGLCHECKFRAMEBUFFERSTATUSPROC CheckFramebufferStatus{};
    PFNGLBLITFRAMEBUFFERPROC BlitFramebuffer{};
    PFNGLGENBUFFERSPROC GenBuffers{};
    PFNGLDELETEBUFFERSPROC DeleteBuffers{};
    PFNGLBINDBUFFERPROC BindBuffer{};
    PFNGLBUFFERDATAPROC BufferData{};
    PFNGLGETUNIFORMLOCATIONPROC GetUniformLocation{};
    PFNGLUNIFORM1IPROC Uniform1i{};
    PFNGLUNIFORM1FPROC Uniform1f{};
    void (APIENTRY *clear_depth_f)(GLfloat){};
    decltype(&::glClearDepth) clear_depth_d{};
    void (APIENTRY *depth_range_f)(GLfloat,GLfloat){};
    decltype(&::glDepthRange) depth_range_d{};
    bool embedded{};
    template<class T>static void load(T& fn,const char* name){
        fn=reinterpret_cast<T>(SDL_GL_GetProcAddress(name));
        if(!fn)throw std::runtime_error(std::string("GL entry unavailable: ")+name);
    }
    void init(bool es){embedded=es;
#define WA2_GL_LOAD(name) load(name,"gl" #name);
        WA2_GL_BASE(WA2_GL_LOAD)
        WA2_GL_LOAD(ActiveTexture) WA2_GL_LOAD(BlendFuncSeparate)
        WA2_GL_LOAD(BlendEquationSeparate) WA2_GL_LOAD(BlendColor)
        WA2_GL_LOAD(GenFramebuffers) WA2_GL_LOAD(DeleteFramebuffers)
        WA2_GL_LOAD(BindFramebuffer) WA2_GL_LOAD(FramebufferTexture2D)
        WA2_GL_LOAD(CheckFramebufferStatus) WA2_GL_LOAD(BlitFramebuffer)
        WA2_GL_LOAD(GenBuffers) WA2_GL_LOAD(DeleteBuffers) WA2_GL_LOAD(BindBuffer)
        WA2_GL_LOAD(BufferData) WA2_GL_LOAD(GetUniformLocation)
        WA2_GL_LOAD(Uniform1i) WA2_GL_LOAD(Uniform1f)
#undef WA2_GL_LOAD
        if(es){load(clear_depth_f,"glClearDepthf");load(depth_range_f,"glDepthRangef");}
        else{load(clear_depth_d,"glClearDepth");load(depth_range_d,"glDepthRange");}
    }
    void ClearDepth(float value)const{if(embedded)clear_depth_f(value);else clear_depth_d(value);}
    void DepthRange(float a,float b)const{if(embedded)depth_range_f(a,b);else depth_range_d(a,b);}
#undef WA2_GL_BASE
};
}
