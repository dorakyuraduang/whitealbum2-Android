#pragma once
#include "../../generated/graphics-abi/source_abi.hpp"
#include <array>
#include <string>
namespace wa2::graphics {
struct TextureStage {
    std::uint32_t colour_op{},colour_arg1{},colour_arg2{},alpha_op{},alpha_arg1{},alpha_arg2{},transform_flags{};
    bool texture{};
};
struct FixedPipeline {std::array<TextureStage,8> stages;};
// Default stages are compiled by the same generic shader translator as the
// original effect shaders. They use attributes/VBOs on desktop GL and GLES;
// no legacy fixed-function OpenGL entry points are required.
unsigned fixed_pipeline_sample_mask(const FixedPipeline&);
bool fixed_pipeline_texture_batch(const FixedPipeline&,std::uint16_t bound_textures);
std::string fixed_pixel_assembly(const FixedPipeline&,unsigned texture_slots=1);
std::string fixed_vertex_assembly();
std::string movie_pixel_assembly();
std::array<std::array<float,4>,3> movie_colour_constants(bool full_range,bool bt709);
std::array<float,4> fixed_stage_reference(const TextureStage&,std::array<float,4> current,
    const std::array<float,4>& diffuse,const std::array<float,4>& texture,const std::array<float,4>& factor);
std::array<float,4> screen_position(std::array<float,4> xyzw,const abi::D3DVIEWPORT9&);
}
