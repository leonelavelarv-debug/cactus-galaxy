// SPDX-License-Identifier: GPL-3.0-only
// Compact textures (texture_compact.h).
#include "galaxy/gx/texture_compact.h"
#include "galaxy/gx/render_config.h"

#include <cstdlib>
#include <cstring>

namespace galaxy::gx {
namespace {

constexpr std::uint8_t expand5(unsigned v) noexcept { return static_cast<std::uint8_t>((v << 3) | (v >> 2)); }
constexpr std::uint8_t expand6(unsigned v) noexcept { return static_cast<std::uint8_t>((v << 2) | (v >> 4)); }
constexpr std::uint8_t expand4(unsigned v) noexcept { return static_cast<std::uint8_t>(v * 17u); }
constexpr bool fits5(std::uint8_t c) noexcept { return expand5(c >> 3) == c; }
constexpr bool fits6(std::uint8_t c) noexcept { return expand6(c >> 2) == c; }
constexpr bool fits4(std::uint8_t c) noexcept { return expand4(c >> 4) == c; }

// Rounded to the nearest representable value (not truncated: the error stays within half a step).
constexpr unsigned q5(std::uint8_t c) noexcept { return (static_cast<unsigned>(c) * 31u + 127u) / 255u; }
constexpr unsigned q6(std::uint8_t c) noexcept { return (static_cast<unsigned>(c) * 63u + 127u) / 255u; }
constexpr unsigned q4(std::uint8_t c) noexcept { return (static_cast<unsigned>(c) * 15u + 127u) / 255u; }

void store16(std::uint8_t* out, std::uint16_t v) noexcept { std::memcpy(out, &v, 2u); }
std::uint16_t load16(const std::uint8_t* in) noexcept {
    std::uint16_t v;
    std::memcpy(&v, in, 2u);
    return v;
}

// Alpha classes of level 0: every texel opaque, every one 0 or 255, or anything else.
enum class Alpha : std::uint8_t { Opaque, Binary, Any };
Alpha alpha_class(const std::uint8_t* rgba, std::size_t texels) noexcept {
    Alpha result = Alpha::Opaque;
    for (std::size_t i = 0; i < texels; ++i) {
        const std::uint8_t a = rgba[i * 4u + 3u];
        if (a == 255u) continue;
        if (a != 0u) return Alpha::Any;
        result = Alpha::Binary;
    }
    return result;
}

}  // namespace

CompactFormat compact_by_alpha(const std::uint8_t* rgba, std::size_t texels) noexcept {
    if (rgba == nullptr || texels == 0u) return CompactFormat::RGBA8;
    switch (alpha_class(rgba, texels)) {
    case Alpha::Opaque: return CompactFormat::RGB565;
    case Alpha::Binary: return CompactFormat::RGB5A1;
    default: return CompactFormat::RGBA4;
    }
}

CompactFormat compact_for_replacement(unsigned mode, const std::uint8_t* rgba, std::size_t texels) noexcept {
    if (mode == 0u) return CompactFormat::RGBA8;
    const CompactFormat exact = compact_exact(rgba, texels);
    return exact != CompactFormat::RGBA8 || mode < 2u ? exact : compact_by_alpha(rgba, texels);
}

const char* compact_format_name(CompactFormat format) noexcept {
    switch (format) {
    case CompactFormat::R8: return "R8";
    case CompactFormat::RG8: return "RG8";
    case CompactFormat::RGB565: return "RGB565";
    case CompactFormat::RGB5A1: return "RGB5A1";
    case CompactFormat::RGBA4: return "RGBA4";
    default: return "RGBA8";
    }
}

unsigned texture_compact_mode() noexcept {
    if (const char* value = std::getenv("GALAXY_TEXTURE_COMPACT"); value != nullptr && value[0] != '\0') {
        if (std::strcmp(value, "16bits") == 0 || std::strcmp(value, "16") == 0) return 2u;
        if (std::strcmp(value, "exacto") == 0 || std::strcmp(value, "exact") == 0) return 1u;
        if (std::strcmp(value, "no") == 0 || std::strcmp(value, "off") == 0) return 0u;
        const long mode = std::strtol(value, nullptr, 10);
        return mode <= 0 ? 0u : mode >= 2 ? 2u : 1u;
    }
    return get_render_config().texture_compact;
}

CompactFormat compact_for_guest(TexFormat format, TlutFormat tlut, unsigned mode, const std::uint8_t* rgba,
                                std::size_t texels) noexcept {
    if (mode == 0u) return CompactFormat::RGBA8;
    switch (format) {
    case TexFormat::I4:
    case TexFormat::I8: return CompactFormat::R8;
    case TexFormat::IA4:
    case TexFormat::IA8: return CompactFormat::RG8;
    case TexFormat::RGB565: return CompactFormat::RGB565;
    case TexFormat::C4:
    case TexFormat::C8:
    case TexFormat::C14X2:
        if (tlut == TlutFormat::IA8) return CompactFormat::RG8;
        if (tlut == TlutFormat::RGB565) return CompactFormat::RGB565;
        return mode >= 2u ? compact_by_alpha(rgba, texels) : CompactFormat::RGBA8;
    case TexFormat::RGB5A3:
    case TexFormat::CMPR: return mode >= 2u ? compact_by_alpha(rgba, texels) : CompactFormat::RGBA8;
    default: return CompactFormat::RGBA8;
    }
}

CompactFormat compact_exact(const std::uint8_t* rgba, std::size_t texels) noexcept {
    if (rgba == nullptr || texels == 0u) return CompactFormat::RGBA8;
    bool r8 = true, rg8 = true, c565 = true, c5551 = true, c4444 = true;
    for (std::size_t i = 0; i < texels; ++i) {
        const std::uint8_t* p = rgba + i * 4u;
        const std::uint8_t r = p[0], g = p[1], b = p[2], a = p[3];
        const bool grey = r == g && g == b;
        r8 = r8 && grey && a == r;
        rg8 = rg8 && grey;
        const bool colour5 = fits5(r) && fits5(b);
        c565 = c565 && a == 255u && colour5 && fits6(g);
        c5551 = c5551 && (a == 0u || a == 255u) && colour5 && fits5(g);
        c4444 = c4444 && fits4(r) && fits4(g) && fits4(b) && fits4(a);
        if (!r8 && !rg8 && !c565 && !c5551 && !c4444) return CompactFormat::RGBA8;
    }
    if (r8) return CompactFormat::R8;
    if (rg8) return CompactFormat::RG8;
    if (c565) return CompactFormat::RGB565;
    if (c5551) return CompactFormat::RGB5A1;
    if (c4444) return CompactFormat::RGBA4;
    return CompactFormat::RGBA8;
}

void compact_convert(CompactFormat format, const std::uint8_t* rgba, std::size_t texels, std::uint8_t* out) noexcept {
    switch (format) {
    case CompactFormat::R8:
        for (std::size_t i = 0; i < texels; ++i) out[i] = rgba[i * 4u];
        break;
    case CompactFormat::RG8:
        for (std::size_t i = 0; i < texels; ++i) {
            out[i * 2u] = rgba[i * 4u];
            out[i * 2u + 1u] = rgba[i * 4u + 3u];
        }
        break;
    case CompactFormat::RGB565:
        for (std::size_t i = 0; i < texels; ++i) {
            const std::uint8_t* p = rgba + i * 4u;
            store16(out + i * 2u, static_cast<std::uint16_t>((q5(p[0]) << 11) | (q6(p[1]) << 5) | q5(p[2])));
        }
        break;
    case CompactFormat::RGB5A1:
        for (std::size_t i = 0; i < texels; ++i) {
            const std::uint8_t* p = rgba + i * 4u;
            store16(out + i * 2u, static_cast<std::uint16_t>((q5(p[0]) << 11) | (q5(p[1]) << 6) | (q5(p[2]) << 1) |
                                                             (p[3] >= 128u ? 1u : 0u)));
        }
        break;
    case CompactFormat::RGBA4:
        for (std::size_t i = 0; i < texels; ++i) {
            const std::uint8_t* p = rgba + i * 4u;
            store16(out + i * 2u, static_cast<std::uint16_t>((q4(p[0]) << 12) | (q4(p[1]) << 8) | (q4(p[2]) << 4) |
                                                             q4(p[3])));
        }
        break;
    default:
        std::memcpy(out, rgba, texels * 4u);
        break;
    }
}

void compact_expand(CompactFormat format, const std::uint8_t* in, std::size_t texels, std::uint8_t* rgba) noexcept {
    for (std::size_t i = 0; i < texels; ++i) {
        std::uint8_t* p = rgba + i * 4u;
        switch (format) {
        case CompactFormat::R8: p[0] = p[1] = p[2] = p[3] = in[i]; break;
        case CompactFormat::RG8:
            p[0] = p[1] = p[2] = in[i * 2u];
            p[3] = in[i * 2u + 1u];
            break;
        case CompactFormat::RGB565: {
            const std::uint16_t v = load16(in + i * 2u);
            p[0] = expand5(v >> 11);
            p[1] = expand6((v >> 5) & 63u);
            p[2] = expand5(v & 31u);
            p[3] = 255u;
            break;
        }
        case CompactFormat::RGB5A1: {
            const std::uint16_t v = load16(in + i * 2u);
            p[0] = expand5(v >> 11);
            p[1] = expand5((v >> 6) & 31u);
            p[2] = expand5((v >> 1) & 31u);
            p[3] = (v & 1u) != 0u ? 255u : 0u;
            break;
        }
        case CompactFormat::RGBA4: {
            const std::uint16_t v = load16(in + i * 2u);
            p[0] = expand4(v >> 12);
            p[1] = expand4((v >> 8) & 15u);
            p[2] = expand4((v >> 4) & 15u);
            p[3] = expand4(v & 15u);
            break;
        }
        default: std::memcpy(p, in + i * 4u, 4u); break;
        }
    }
}

}  // namespace galaxy::gx
