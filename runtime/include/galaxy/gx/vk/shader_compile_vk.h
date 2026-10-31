// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// HLSL to SPIR-V through glslang's HLSL front end (docs/VULKAN.md). The generated GX shaders and the renderer's own
// shaders keep their D3D12 register declarations; annotate() adds the Vulkan binding each register maps to, so the
// D3D12 sources (and the hashes their caches use) stay as they are.

#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace galaxy::gx::vk {

enum class ShaderStage { Vertex, Geometry, Fragment, Compute };

// One D3D12 register's Vulkan binding: `reg` as written in the source ("b0", "t0, space1", "s0"); a push-constant
// block when `push_constant` is set.
struct RegisterBinding {
    std::string_view reg;
    std::uint32_t set;
    std::uint32_t binding;
    bool push_constant = false;
};

// The GX draw layout (RendererVk): set 0 = b0 and b1 (dynamic uniform buffers), t0 space1 (the matrix palette, a
// dynamic storage buffer), t1 and t2 space1 (vertex blending's earlier vertices and verdicts, storage buffers); set
// 1 = t0-t7 (textures); set 2 = s0-s7 (samplers); b2 (vertex blending's per-draw values) = vertex push constants.
[[nodiscard]] std::span<const RegisterBinding> gx_draw_bindings() noexcept;

// Inserts the Vulkan attribute before every resource declared with a mapped register.
[[nodiscard]] std::string annotate(std::string_view hlsl, std::span<const RegisterBinding> bindings);

// A GX vertex shader (VSOut, `VSOut o = (VSOut)0;`) that also writes a one-pixel point size: Vulkan points drawn
// without the expanding geometry shader need the vertex stage to write PointSize.
[[nodiscard]] inline std::string with_point_size(std::string_view vertex_hlsl) {
    // Inline: the GLES build without the translator (shader_library_gles.h) keys its GLSL on this same text.
    std::string text(vertex_hlsl);
    const std::size_t output = text.find("struct VSOut {");
    const std::size_t close = output == std::string::npos ? output : text.find("};", output);
    static constexpr std::string_view kInit = "VSOut o = (VSOut)0;";
    const std::size_t init = text.find(kInit);
    if (close == std::string::npos || init == std::string::npos) {
        return text;
    }
    text.insert(init + kInit.size(), "\n    o.psize = 1.0;");
    text.insert(close, "    [[vk::builtin(\"PointSize\")]] float psize : PSIZE;\n");
    return text;
}

struct CompileOptions {
    // Run the SPIR-V validator and fail on any message (the tests; the runtime trusts its sources).
    bool validate = false;
    // The pixel shader's SV_Target1 is the second source of dual-source blending: Vulkan reads it at location 0,
    // index 1, while glslang places it at location 1 and ignores location attributes on output-struct members, so
    // the module's decorations are rewritten.
    bool dual_source = false;
    // Keep the debug names (the tests read constant-buffer member names); the runtime strips them (Adreno).
    bool keep_names = false;
};

// Changes with anything that changes compile_hlsl's output for the same source (cached SPIR-V is keyed on it).
inline constexpr std::uint32_t kCompilerRevision = 6;  // 6: vertex blending inputs; 4: debug names stripped

// SPIR-V for `entry` of `hlsl`; empty with `error` set when it does not compile. `defines` are NAME=VALUE. Stage
// inputs and outputs sit at their semantics' locations, so stages and the vertex layout match as D3D12's do by name:
// vertex attributes POSITION 0, NORMAL 1, TANGENT 2, BINORMAL 3, COLOR0-1 4-5, TEXCOORD0-7 6-13, BLENDINDICES 14;
// between stages COLOR0-1 0-1 and TEXCOORDn 2+n; pixel outputs SV_TARGETn n.
[[nodiscard]] std::vector<std::uint32_t> compile_hlsl(std::string_view hlsl, ShaderStage stage, const char* entry,
                                                      std::initializer_list<const char*> defines, std::string& error,
                                                      CompileOptions options = {});

}  // namespace galaxy::gx::vk
