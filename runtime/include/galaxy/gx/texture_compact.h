// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Compact textures (GLES, low-memory consoles): the guest's textures and the texture packs' replacements on the GPU in
// formats narrower than RGBA8 when their texels fit, so they take a half or a quarter of the memory and of the
// bandwidth each sample reads (on the RG35XX H the 1 GB is shared with the Mali-G31, and its frames are bound by it).
//
//   R8   intensity (I4, I8): one byte, sampled as (i, i, i, i) through the texture's swizzle
//   RG8  intensity + alpha (IA4, IA8, palettes in IA8): two bytes, sampled as (i, i, i, a)
//   RGB565, RGB5A1, RGBA4: two bytes
//
// GALAXY_TEXTURE_COMPACT (texture_compact= in config.ini): 0 off; 1 exact (the default on GLES: only what fits with no
// change at all, by the guest format: I*, IA*, RGB565 and palettes in IA8 or RGB565; a replacement when every texel of
// it fits); 2 16 bits (also RGB5A3, CMPR and their palettes in RGB565, RGB5A1 or RGBA4 by their alpha: the colours lose
// their lowest bits, which CMPR's interpolation and the 5-bit colours of RGB5A3 barely use). RGBA8 textures and EFB
// copies stay RGBA8. `nopal texturas` (Nopal) quantizes a pack on the PC so its textures fit exactly.
// The checks and conversions are plain CPU code (no GL), shared by the GLES backend and the tests.
#include "galaxy/gx/gx_bitfields.h"
#include <cstddef>
#include <cstdint>

namespace galaxy::gx {

enum class CompactFormat : std::uint8_t { RGBA8 = 0, R8, RG8, RGB565, RGB5A1, RGBA4 };

[[nodiscard]] constexpr std::uint32_t compact_texel_bytes(CompactFormat format) noexcept {
    return format == CompactFormat::RGBA8 ? 4u : format == CompactFormat::R8 ? 1u : 2u;
}
[[nodiscard]] const char* compact_format_name(CompactFormat format) noexcept;

// 0 off, 1 exact, 2 16 bits (GALAXY_TEXTURE_COMPACT, else the render config's texture_compact).
[[nodiscard]] unsigned texture_compact_mode() noexcept;

// The format for a guest texture whose levels decode to `rgba` (level 0, w*h texels; read only for the formats whose
// choice depends on their alpha, in mode 2).
[[nodiscard]] CompactFormat compact_for_guest(TexFormat format, TlutFormat tlut, unsigned mode,
                                              const std::uint8_t* rgba, std::size_t texels) noexcept;

// The narrowest format that holds every texel exactly (the 5- and 6-bit channels as the guest decoder expands them:
// bit replication). RGBA8 when none does.
[[nodiscard]] CompactFormat compact_exact(const std::uint8_t* rgba, std::size_t texels) noexcept;

// By the alpha of level 0: RGB565 when every texel is opaque, RGB5A1 when each is 0 or 255, else RGBA4 (mode 2).
[[nodiscard]] CompactFormat compact_by_alpha(const std::uint8_t* rgba, std::size_t texels) noexcept;

// A texture pack's replacement (level 0): compact_exact; in mode 2, compact_by_alpha when nothing fits exactly.
[[nodiscard]] CompactFormat compact_for_replacement(unsigned mode, const std::uint8_t* rgba,
                                                    std::size_t texels) noexcept;

// compact_exact over several images at once (a replacement's mip levels: generated and custom): the narrowest format
// that holds every texel of every one exactly; RGBA8 when none does. `count` images, `rgba[i]` of `texels[i]`.
[[nodiscard]] CompactFormat compact_exact_levels(const std::uint8_t* const* rgba, const std::size_t* texels,
                                                 std::size_t count) noexcept;

// A replacement with all its mip levels: in mode 1 ("exacto") the format holds every level exactly (a lower level
// with alpha the base lacks keeps it: RGBA8 when no compact format holds them all); modes 0 and 2 as
// compact_for_replacement on level 0.
[[nodiscard]] CompactFormat compact_for_replacement_levels(unsigned mode, const std::uint8_t* const* rgba,
                                                           const std::size_t* texels, std::size_t count) noexcept;

// RGBA8 texels as `format` (tightly packed; 16-bit texels in the machine's byte order, as GL reads them).
void compact_convert(CompactFormat format, const std::uint8_t* rgba, std::size_t texels, std::uint8_t* out) noexcept;

// The RGBA8 a compact texel samples as (tests, and the CPU's view of an uploaded texture).
void compact_expand(CompactFormat format, const std::uint8_t* in, std::size_t texels, std::uint8_t* rgba) noexcept;

}  // namespace galaxy::gx
