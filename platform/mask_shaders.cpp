#include "mask_shaders.hpp"
namespace wa2::platform {
MaskShaderSources mask_shader_sources(bool embedded){
    const std::string version=embedded?"#version 100\n":"#version 120\n";
    return {version+
        "attribute vec2 position;attribute vec2 texcoord;varying vec2 uv;uniform float target;"
        "void main(){uv=texcoord;gl_Position=vec4(position.x,position.y*target,0.0,1.0);}",
        version+"#ifdef GL_ES\nprecision highp float;\n#endif\n"
        "varying vec2 uv;uniform sampler2D image;uniform sampler2D mask;"
        "uniform float bias;uniform float gain;uniform vec3 flags;"
        "void main(){vec2 m=mix(uv,vec2(1.0)-uv,flags.xy);float a=texture2D(mask,m).a;"
        "a=mix(a,1.0-a,flags.z);vec4 colour=texture2D(image,uv);"
        "colour.a=clamp((a+bias)*gain,0.0,1.0);gl_FragColor=colour;}"};
}
}
