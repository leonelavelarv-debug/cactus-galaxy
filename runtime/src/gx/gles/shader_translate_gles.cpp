// SPDX-License-Identifier: GPL-3.0-only
// HLSL to GLSL ES 3.20 or 3.10 (shader_translate_gles.h): glslang's HLSL front end, then SPIRV-Cross.

#include "galaxy/gx/gles/shader_translate_gles.h"

#include "galaxy/gx/vk/shader_compile_vk.h"

#include <spirv_glsl.hpp>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <vector>

namespace galaxy::gx::gles {

namespace {

bool is_identifier_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

// The index of the bracket closing the one at `open` (same kind), or npos.
std::size_t matching(const std::string& text, std::size_t open, char open_char, char close_char) {
    int depth = 0;
    for (std::size_t at = open; at < text.size(); ++at) {
        if (text[at] == open_char) {
            ++depth;
        } else if (text[at] == close_char && --depth == 0) {
            return at;
        }
    }
    return std::string::npos;
}

// Interpolated redraw's vertex blending out: its cbuffer (b2), storage buffers (t1, t2 space1) and helpers, down to
// the end of vertex_blend(), become the values a game frame has (t = 1: the draw's own vertices).
void remove_vertex_blend(std::string& text) {
    const std::size_t start = text.find("cbuffer GxVertexBlend");
    if (start == std::string::npos) {
        return;
    }
    const std::size_t function = text.find("void vertex_blend(", start);
    const std::size_t body = function == std::string::npos ? function : text.find('{', function);
    const std::size_t end = body == std::string::npos ? body : matching(text, body, '{', '}');
    if (end == std::string::npos) {
        return;
    }
    static constexpr char kStub[] =
        "static float vb_t = 1.0;\n"
        "static float vb_t_prev = 1.0;\n"
        "static float2 vb_jitter = float2(0.0, 0.0);\n"
        "static float3 vb_pos_prev = float3(0.0, 0.0, 0.0);\n"
        "void vertex_blend(inout VSIn v, uint id) { vb_pos_prev = v.pos; }\n";
    text.replace(start, end + 1u - start, kStub);
}

// `name[expr]` -> `replacement(expr)` for every use of the identifier `name` followed by a bracket.
void index_to_call(std::string& text, const std::string& name, const std::string& replacement) {
    for (std::size_t at = text.find(name); at != std::string::npos; at = text.find(name, at)) {
        const bool starts = at == 0u || !is_identifier_char(text[at - 1u]);
        std::size_t after = at + name.size();
        while (after < text.size() && (text[after] == ' ' || text[after] == '\t')) {
            ++after;
        }
        if (!starts || after >= text.size() || text[after] != '[') {
            at += name.size();
            continue;
        }
        const std::size_t close = matching(text, after, '[', ']');
        if (close == std::string::npos) {
            return;
        }
        text[close] = ')';
        text.replace(at, after + 1u - at, replacement + "(");
        at += replacement.size() + 1u;
    }
}

// `name[d]` (a literal digit) -> `name_d`.
void literal_index_to_name(std::string& text, const std::string& name) {
    for (std::size_t at = text.find(name); at != std::string::npos; at = text.find(name, at)) {
        const bool starts = at == 0u || !is_identifier_char(text[at - 1u]);
        const std::size_t open = at + name.size();
        if (starts && open + 2u < text.size() && text[open] == '[' &&
            std::isdigit(static_cast<unsigned char>(text[open + 1u])) != 0 && text[open + 2u] == ']') {
            text.replace(open, 3u, std::string("_") + text[open + 1u]);
        }
        at = open;
    }
}

// Replaces the declaration `<type> <name>[8] : register(<reg>0);` (any spacing) by eight single declarations.
void split_array_declaration(std::string& text, const char* type, const char* name, char reg) {
    for (std::size_t at = text.find(type); at != std::string::npos; at = text.find(type, at + 1u)) {
        std::size_t p = at + std::strlen(type);
        while (p < text.size() && (text[p] == ' ' || text[p] == '\t')) ++p;
        if (text.compare(p, std::strlen(name), name) != 0) continue;
        p += std::strlen(name);
        if (text.compare(p, 3u, "[8]") != 0) continue;
        const std::size_t semicolon = text.find(';', p);
        if (semicolon == std::string::npos) return;
        std::string declarations;
        for (int i = 0; i < 8; ++i) {
            declarations += std::string(type) + " " + name + "_" + std::to_string(i) + " : register(" + reg +
                std::to_string(i) + ");\n";
        }
        text.replace(at, semicolon + 1u - at, declarations);
        return;
    }
}

constexpr vk::RegisterBinding kGxStorageBindings[] = {
    {"b0", 0, 0}, {"b1", 0, 1}, {"t0, space1", 0, 0},
    {"t0", 1, 0}, {"t1", 1, 1}, {"t2", 1, 2}, {"t3", 1, 3}, {"t4", 1, 4}, {"t5", 1, 5}, {"t6", 1, 6}, {"t7", 1, 7},
    {"s0", 2, 0}, {"s1", 2, 1}, {"s2", 2, 2}, {"s3", 2, 3}, {"s4", 2, 4}, {"s5", 2, 5}, {"s6", 2, 6}, {"s7", 2, 7},
};
constexpr vk::RegisterBinding kGxTextureBufferBindings[] = {
    {"b0", 0, 0}, {"b1", 0, 1}, {"t0, space1", 3, kPaletteTextureUnit},
    {"t0", 1, 0}, {"t1", 1, 1}, {"t2", 1, 2}, {"t3", 1, 3}, {"t4", 1, 4}, {"t5", 1, 5}, {"t6", 1, 6}, {"t7", 1, 7},
    {"s0", 2, 0}, {"s1", 2, 1}, {"s2", 2, 2}, {"s3", 2, 3}, {"s4", 2, 4}, {"s5", 2, 5}, {"s6", 2, 6}, {"s7", 2, 7},
};
// GLSL ES 3.00 (an OpenGL ES 3.0 driver: no storage or texture buffers): the palette as a 2D RGBA32UI texture at
// unit 8, kPaletteTextureWidth texels a row (RendererGles mirrors the bound palette range into it).
constexpr vk::RegisterBinding kGxTexture2DBindings[] = {
    {"b0", 0, 0}, {"b1", 0, 1}, {"t0, space1", 1, kPaletteTextureUnit},
    {"t0", 1, 0}, {"t1", 1, 1}, {"t2", 1, 2}, {"t3", 1, 3}, {"t4", 1, 4}, {"t5", 1, 5}, {"t6", 1, 6}, {"t7", 1, 7},
    {"s0", 2, 0}, {"s1", 2, 1}, {"s2", 2, 2}, {"s3", 2, 3}, {"s4", 2, 4}, {"s5", 2, 5}, {"s6", 2, 6}, {"s7", 2, 7},
};
constexpr vk::RegisterBinding kUtilityBindings[] = {
    {"b0", 0, 0}, {"t0", 1, 0}, {"s0", 2, 0}, {"s1", 2, 1},
};

}  // namespace

std::string prepare_hlsl(std::string_view hlsl, ShaderKind kind, const TranslateOptions& options) {
    std::string text(hlsl);
    if (kind != ShaderKind::Gx) {
        return text;
    }
    remove_vertex_blend(text);
    if (options.glsl_version == 300 || !options.palette_storage_block) {
        static constexpr char kDeclaration[] = "StructuredBuffer<float4> mtx_palette : register(t0, space1);";
        if (const std::size_t at = text.find(kDeclaration); at != std::string::npos) {
            index_to_call(text, "mtx_palette", "mtx_palette_load");
            const std::size_t declaration = text.find(kDeclaration);
            if (options.glsl_version == 300) {
                // No buffer textures in OpenGL ES 3.0: a 2D texture, texel i at (i % width, i / width).
                char load[256];
                std::snprintf(load, sizeof(load),
                              "Texture2D<uint4> mtx_palette_bits : register(t0, space1);\n"
                              "float4 mtx_palette_load(uint i) { return asfloat(mtx_palette_bits.Load("
                              "int3(int(i & %uu), int(i >> %uu), 0))); }",
                              kPaletteTextureWidth - 1u, kPaletteTextureWidthLog2);
                text.replace(declaration, std::strlen(kDeclaration), load);
            } else {
                text.replace(declaration, std::strlen(kDeclaration),
                             "Buffer<uint4> mtx_palette_bits : register(t0, space1);\n"
                             "float4 mtx_palette_load(uint i) { return asfloat(mtx_palette_bits.Load(int(i))); }");
            }
        }
    }
    split_array_declaration(text, "Texture2D", "tex", 't');
    split_array_declaration(text, "SamplerState", "samp", 's');
    literal_index_to_name(text, "tex");
    literal_index_to_name(text, "samp");
    return text;
}

std::string translate(std::string_view hlsl, Stage stage, ShaderKind kind, const TranslateOptions& options,
                      std::string& error) {
    const std::string prepared = prepare_hlsl(hlsl, kind, options);
    const std::span<const vk::RegisterBinding> bindings = kind == ShaderKind::Utility
        ? std::span<const vk::RegisterBinding>(kUtilityBindings)
        : options.glsl_version == 300   ? std::span<const vk::RegisterBinding>(kGxTexture2DBindings)
        : options.palette_storage_block ? std::span<const vk::RegisterBinding>(kGxStorageBindings)
                                        : std::span<const vk::RegisterBinding>(kGxTextureBufferBindings);
    const std::string annotated = vk::annotate(prepared, bindings);
    vk::CompileOptions compile_options;
    compile_options.keep_names = options.keep_names;
    std::vector<std::uint32_t> spirv = vk::compile_hlsl(
        annotated, stage == Stage::Vertex ? vk::ShaderStage::Vertex : vk::ShaderStage::Fragment, "main", {}, error,
        compile_options);
    if (spirv.empty()) {
        return {};
    }
    try {
        spirv_cross::CompilerGLSL glsl(std::move(spirv));
        spirv_cross::CompilerGLSL::Options common = glsl.get_common_options();
        common.version = static_cast<std::uint32_t>(
            options.glsl_version == 300 ? 300 : options.glsl_version == 310 ? 310 : 320);
        common.es = true;
        common.vertex.fixup_clipspace = stage == Stage::Vertex;
        common.vertex.flip_vert_y = stage == Stage::Vertex;
        common.vertex.support_nonzero_base_instance = false;
        common.fragment.default_float_precision = spirv_cross::CompilerGLSL::Options::Highp;
        common.fragment.default_int_precision = spirv_cross::CompilerGLSL::Options::Highp;
        glsl.set_common_options(common);
        const spirv_cross::VariableID dummy = glsl.build_dummy_sampler_for_combined_images();
        glsl.build_combined_image_samplers();
        for (const spirv_cross::CombinedImageSampler& pair : glsl.get_combined_image_samplers()) {
            const bool real_sampler = pair.sampler_id != dummy;
            const std::uint32_t unit = real_sampler
                ? glsl.get_decoration(pair.sampler_id, spv::DecorationBinding)
                : glsl.get_decoration(pair.image_id, spv::DecorationBinding);
            glsl.set_decoration(pair.combined_id, spv::DecorationBinding, unit);
        }
        const bool es300 = options.glsl_version == 300;
        if (es300) {
            // The combined samplers by name (their uniforms are found by it after linking: apply_bindings).
            for (const spirv_cross::CombinedImageSampler& pair : glsl.get_combined_image_samplers()) {
                std::string name = "nb_" + glsl.get_name(pair.image_id);
                if (pair.sampler_id != dummy) {
                    name += "_" + glsl.get_name(pair.sampler_id);
                }
                if (name == "nb_" || name.find("__") != std::string::npos) {
                    name = "nb_sampler_" + std::to_string(static_cast<std::uint32_t>(pair.combined_id));
                }
                glsl.set_name(pair.combined_id, name);
            }
            // No layout(early_fragment_tests) before GLSL ES 3.10: the tests run where the driver puts them, which
            // differs from GX's early Z only for a fragment the alpha test discards while depth is written
            // (docs/GX-ES.md counts those programs).
            if (stage == Stage::Fragment) {
                glsl.unset_execution_mode(spv::ExecutionModeEarlyFragmentTests);
            }
            // GLSL ES 3.00 has no layout(location) between stages: both sides name each varying by its location.
            const spirv_cross::ShaderResources io = glsl.get_shader_resources();
            const auto& varyings = stage == Stage::Vertex ? io.stage_outputs : io.stage_inputs;
            for (const spirv_cross::Resource& v : varyings) {
                if (glsl.has_decoration(v.id, spv::DecorationLocation)) {
                    glsl.set_name(v.id, "nv_" + std::to_string(glsl.get_decoration(v.id, spv::DecorationLocation)));
                }
            }
        }
        std::string source = glsl.compile();
        if (source.empty()) {
            error = "SPIRV-Cross produced no GLSL";
            return source;
        }
        if (es300) {
            // Nor layout(binding): the blocks' bindings and the samplers' units, for apply_bindings after linking.
            const spirv_cross::ShaderResources resources = glsl.get_shader_resources();
            for (const spirv_cross::Resource& block : resources.uniform_buffers) {
                source += "// nebula-bind block " + glsl.get_name(block.base_type_id) + " " +
                          std::to_string(glsl.get_decoration(block.id, spv::DecorationBinding)) + "\n";
            }
            // The combined samplers (every texture of these shaders: the HLSL has separate ones).
            for (const spirv_cross::Resource& image : resources.sampled_images) {
                source += "// nebula-bind sampler " + glsl.get_name(image.id) + " " +
                          std::to_string(glsl.get_decoration(image.id, spv::DecorationBinding)) + "\n";
            }
        }
        return source;
    } catch (const std::exception& failure) {
        error = std::string("SPIRV-Cross: ") + failure.what();
        return {};
    }
}

}  // namespace galaxy::gx::gles
