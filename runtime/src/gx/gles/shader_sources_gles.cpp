// SPDX-License-Identifier: GPL-3.0-only
// The HLSL of the GLES port's programs (shader_sources_gles.h).
#include "galaxy/gx/gles/shader_sources_gles.h"

#include "galaxy/gx/renderer_common.h"
#include "galaxy/gx/shader_gen.h"
#include "galaxy/gx/uber_shaders.h"
#include "galaxy/gx/vk/shader_compile_vk.h"

namespace galaxy::gx::gles {

ShaderGenOptions gles_generator_options() noexcept {
    ShaderGenOptions options{};
    options.scalar_byte_masks = true;  // Mali (device_quirks_vk.cpp)
    return options;
}

ProgramHlsl program_hlsl(const VertexShaderKey& vs_key, const PixelShaderKey& ps_key, bool points) {
    ShaderGenerator generator;
    generator.set_options(gles_generator_options());
    ProgramHlsl source;
    source.vs = generator.generate_vs(vs_key);
    if (points) {
        source.vs = vk::with_point_size(source.vs);
    }
    source.ps = generator.generate_ps(ps_key);
    return source;
}

std::string uber_vertex_hlsl() { return std::string(uber_vs_source()); }

std::string uber_pixel_hlsl(unsigned variant) {
    std::string ps;
    ps += uber_ps_variant_dual_source(variant) ? "#define UBER_DUAL_SRC 1\n" : "#define UBER_DUAL_SRC 0\n";
    ps += uber_ps_variant_export_depth(variant) ? "#define UBER_EXPORT_DEPTH 1\n" : "#define UBER_EXPORT_DEPTH 0\n";
    ps += uber_ps_variant_early_depth(variant) ? "#define UBER_EARLY_DEPTH 1\n" : "#define UBER_EARLY_DEPTH 0\n";
    ps += uber_ps_source();
    return ps;
}

std::string_view utility_vertex_hlsl() noexcept { return blit_vertex_shader_source(); }

std::string_view conversion_hlsl() noexcept { return efb_conversion_shader_source(); }

std::string display_copy_hlsl() {
    // The EFB conversion with the display copy's filters after its tap, their values in a fifth constant vector
    // (copy_filter: strength, shadows, contrast, anti-aliasing): see create_utility_programs.
    std::string source(efb_conversion_shader_source());
    const std::string tap = "tex_sample = srctex.SampleLevel(s_lin, sample_pos * inv, 0);";
    const std::string members = "    float4 filt2;\n};";
    const std::size_t at = source.find(tap);
    const std::size_t block = source.find(members);
    if (at == std::string::npos || block == std::string::npos || block > at) {
        return {};
    }
    const std::string filters = tap + R"HLSL(
        {
            const float4 nw = srctex.SampleLevel(s_lin, (sample_pos + float2(-0.5, -0.5)) * inv, 0);
            const float4 ne = srctex.SampleLevel(s_lin, (sample_pos + float2( 0.5, -0.5)) * inv, 0);
            const float4 sw = srctex.SampleLevel(s_lin, (sample_pos + float2(-0.5,  0.5)) * inv, 0);
            const float4 se = srctex.SampleLevel(s_lin, (sample_pos + float2( 0.5,  0.5)) * inv, 0);
            const float3 luma = float3(0.299, 0.587, 0.114);
            const float l_nw = dot(nw.rgb, luma), l_ne = dot(ne.rgb, luma);
            const float l_sw = dot(sw.rgb, luma), l_se = dot(se.rgb, luma);
            const float l_m = dot(tex_sample.rgb, luma);
            const float l_min = min(l_m, min(min(l_nw, l_ne), min(l_sw, l_se)));
            const float l_max = max(l_m, max(max(l_nw, l_ne), max(l_sw, l_se)));
            const bool edge = copy_filter.w > 0.5 && (l_max - l_min) > max(1.0 / 32.0, l_max * 0.125);
            [branch] if (edge) {
                float2 dir = float2(-((l_nw + l_ne) - (l_sw + l_se)), (l_nw + l_sw) - (l_ne + l_se));
                const float reduce = max((l_nw + l_ne + l_sw + l_se) * (0.25 / 8.0), 1.0 / 128.0);
                dir = clamp(dir / (min(abs(dir.x), abs(dir.y)) + reduce), -4.0, 4.0);
                const float3 a = 0.5 * (srctex.SampleLevel(s_lin, (sample_pos + dir * (1.0 / 3.0 - 0.5)) * inv, 0).rgb +
                                        srctex.SampleLevel(s_lin, (sample_pos + dir * (2.0 / 3.0 - 0.5)) * inv, 0).rgb);
                const float3 b = a * 0.5 + 0.25 * (srctex.SampleLevel(s_lin, (sample_pos - dir * 0.5) * inv, 0).rgb +
                                                   srctex.SampleLevel(s_lin, (sample_pos + dir * 0.5) * inv, 0).rgb);
                const float l_b = dot(b, luma);
                tex_sample.rgb = (l_b < l_min || l_b > l_max) ? a : b;
            } else {
                const float3 lo = min(min(min(nw.rgb, ne.rgb), min(sw.rgb, se.rgb)), tex_sample.rgb);
                const float3 hi = max(max(max(nw.rgb, ne.rgb), max(sw.rgb, se.rgb)), tex_sample.rgb);
                const float3 blur = (nw.rgb + ne.rgb + sw.rgb + se.rgb) * 0.25;
                tex_sample.rgb = clamp(tex_sample.rgb + copy_filter.x * (tex_sample.rgb - blur), lo, hi);
            }
            [branch] if (copy_filter.y > 0.0 || copy_filter.z != 0.0) {
                const float l = dot(tex_sample.rgb, luma);
                float graded = l + copy_filter.y * l * (1.0 - l) * (1.0 - l);
                graded = saturate((graded - 0.5) * (1.0 + copy_filter.z) + 0.5);
                tex_sample.rgb = l > 1.0 / 1024.0 ? saturate(tex_sample.rgb * (graded / l))
                                                  : saturate(tex_sample.rgb + (graded - l));
            }
        })HLSL";
    source.replace(at, tap.size(), filters);
    source.replace(block, members.size(), "    float4 filt2;\n    float4 copy_filter;\n};");
    return source;
}

}  // namespace galaxy::gx::gles
