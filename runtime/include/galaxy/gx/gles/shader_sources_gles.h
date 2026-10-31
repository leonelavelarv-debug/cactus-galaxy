// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// The HLSL of every program the GLES port builds, made one way for the pipeline cache and the renderer (on the
// console) and for nebula_glsl_aot (on the PC): the GLSL library (shader_library_gles.h) finds its entries by this
// text, so both sides must write it identically.
#include "galaxy/gx/shader_gen.h"
#include "galaxy/gx/shader_keys.h"

#include <string>
#include <string_view>

namespace galaxy::gx::gles {

// The generator's options on GLES.
[[nodiscard]] ShaderGenOptions gles_generator_options() noexcept;

struct ProgramHlsl {
    std::string vs;
    std::string ps;
};
// A GX program: its shader keys, and `points` (a point list: the vertex shader also writes the point size).
[[nodiscard]] ProgramHlsl program_hlsl(const VertexShaderKey& vs_key, const PixelShaderKey& ps_key, bool points);

// The uber shaders (uber_shaders.h), pixel variant 0..kUberPsVariants-1.
[[nodiscard]] std::string uber_vertex_hlsl();
[[nodiscard]] std::string uber_pixel_hlsl(unsigned variant);

// The renderer's own: the fullscreen vertex shader, the EFB conversion, and the display copy's conversion with its
// filters (anti-aliasing, sharpening, shadows and contrast; their values in a fifth constant vector, copy_filter).
[[nodiscard]] std::string_view utility_vertex_hlsl() noexcept;
[[nodiscard]] std::string_view conversion_hlsl() noexcept;
[[nodiscard]] std::string display_copy_hlsl();

}  // namespace galaxy::gx::gles
