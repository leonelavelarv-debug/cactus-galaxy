// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// The GLSL library: every shader the GLES port uses, already in GLSL ES, so the console hands it straight to the
// driver with no translator. The HLSL the generator, the uber shaders and the renderer write is translated on the PC
// (nebula_glsl_aot, with shader_translate_gles.h: glslang + SPIRV-Cross) for the keys a whole tour of the game uses
// (platform/android/vk_seed_keys.bin) and the ones a console session recorded (gles_keys.bin), in each form a driver
// may need (GLSL ES 3.20 or 3.10; the matrix palette as a storage block or a texture buffer). An entry is found by
// the hash of its HLSL and that form: a generator change misses instead of using stale GLSL.
//
// The file (GALAXY_GLES_SHADER_LIBRARY; smg/shaders-gles.bin in the port), little endian:
//   "NGLS", version, entry count, block count
//   entries, sorted by key: key (u64), block (u32), offset in the block (u32), length (u32), reserved (u32)
//   blocks: compressed size (u32), raw size (u32), zlib data (several shaders each, so their shared text compresses)
//
// Built with NEBULA_GLES_TRANSLATOR=OFF (the console's build) the translator is not in the binary: a GX program the
// library lacks is drawn with the uber shader (which the library always has), and its key is recorded as before so
// the next library has it (nopal nebula shaders).
#include "galaxy/gx/gles/shader_translate_gles.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace galaxy::gx::gles {

// The entry key of a shader: its HLSL and the form of the GLSL (the palette form only matters to GX shaders).
[[nodiscard]] std::uint64_t library_key(std::string_view hlsl, Stage stage, ShaderKind kind,
                                        const TranslateOptions& options) noexcept;

// Whether this build has the translator (NEBULA_GLES_TRANSLATOR, on unless the console's build turns it off).
[[nodiscard]] bool translator_available() noexcept;

// The GLSL for a shader: from the library (GALAXY_GLES_SHADER_LIBRARY, opened on the first call), else translated
// here when this build has the translator, else empty with `error` set. Any thread.
[[nodiscard]] std::string shader_glsl(std::string_view hlsl, Stage stage, ShaderKind kind,
                                      const TranslateOptions& options, std::string& error);

struct LibraryStats {
    bool open = false;
    std::uint64_t entries = 0;
    std::uint64_t hits = 0;
    std::uint64_t misses = 0;        // not in the library
    std::uint64_t translations = 0;  // misses translated here (a build with the translator)
};
[[nodiscard]] LibraryStats library_stats() noexcept;

// Writing a library (nebula_glsl_aot): entries with distinct keys, in the order given (similar shaders next to each
// other compress best). False with `error` when it could not be written.
bool write_shader_library(const std::filesystem::path& file, std::vector<std::pair<std::uint64_t, std::string>> entries,
                          std::string& error);

// Blending of GX's dual-source alpha in the fragment shader (docs/GX_ES_FRAMEBUFFER_FETCH.md). A pixel shader
// of a key with flags bit 7 writes the EFB's colour at location 0 (its alpha: GXSetDstAlpha's constant, or the
// TEV alpha at the RGBA6 EFB's 6 bits) and the 8-bit TEV alpha the blender reads at location 1. Without
// GL_EXT_blend_func_extended (the Mali-G31's driver) the second output cannot reach the blender; with framebuffer
// fetch the shader reads the EFB's colour and blends itself, the GX factors of `blend_bits` (RenderStateKey: source
// factor bits 4-2, destination 7-5, constant alpha bit 28) baked in, exactly as D3D12 and Vulkan blend with
// SRC1_ALPHA. The pipeline then draws with blending off (its write masks stay).
enum class FramebufferFetch : std::uint8_t { None, Arm, Ext };  // GL_ARM_/GL_EXT_shader_framebuffer_fetch
// The fetch form of such a pixel shader's GLSL (any version the library has), or empty with `error` set when the
// GLSL does not have the two outputs it expects.
[[nodiscard]] std::string fetch_blend_glsl(std::string_view ps_glsl, std::uint32_t blend_bits, FramebufferFetch fetch,
                                           std::string& error);

// Reading one (tests): the GLSL of every entry, by key.
bool read_shader_library(const std::filesystem::path& file, std::vector<std::pair<std::uint64_t, std::string>>& out,
                         std::string& error);

}  // namespace galaxy::gx::gles
