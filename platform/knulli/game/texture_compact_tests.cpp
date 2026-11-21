// SPDX-License-Identifier: GPL-3.0-only
// Compact textures (texture_compact.h): the choices by guest format and by content, the conversions against their
// expansion (every 4-, 5- and 6-bit value comes back as it went), and, against the live driver (SDL2;
// SDL_VIDEODRIVER=offscreen off the handheld), each compact format made, filled through the texture backend from RGBA8
// staging (record_upload) and from staging already in its format (record_upload_compact), and read back through a
// framebuffer. Exit status 0 when all pass.

#include "sdl_host.h"

#include "galaxy/gx/gles/gles_context.h"
#include "galaxy/gx/gles/renderer_gles.h"
#include "galaxy/gx/texture_backend.h"
#include "galaxy/gx/texture_compact.h"
#include "galaxy/gx/texture_pack.h"

#include <filesystem>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <vector>

using namespace galaxy::gx;
using namespace galaxy::gx::gles::gl;

namespace {

int g_failures = 0;

void expect(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

std::vector<std::uint8_t> texels(std::initializer_list<std::uint32_t> rgba) {
    std::vector<std::uint8_t> out;
    for (std::uint32_t v : rgba) {
        out.push_back(static_cast<std::uint8_t>(v >> 24));
        out.push_back(static_cast<std::uint8_t>(v >> 16));
        out.push_back(static_cast<std::uint8_t>(v >> 8));
        out.push_back(static_cast<std::uint8_t>(v));
    }
    return out;
}

constexpr std::uint8_t e5(unsigned v) { return static_cast<std::uint8_t>((v << 3) | (v >> 2)); }
constexpr std::uint8_t e6(unsigned v) { return static_cast<std::uint8_t>((v << 2) | (v >> 4)); }

// Converts and expands back; true when every texel returns unchanged.
bool round_trip(CompactFormat format, const std::vector<std::uint8_t>& rgba) {
    const std::size_t n = rgba.size() / 4u;
    std::vector<std::uint8_t> packed(n * compact_texel_bytes(format)), back(rgba.size());
    compact_convert(format, rgba.data(), n, packed.data());
    compact_expand(format, packed.data(), n, back.data());
    return back == rgba;
}

void cpu_tests() {
    // By guest format, mode 1 (exact).
    expect(compact_for_guest(TexFormat::I4, TlutFormat::IA8, 1u, nullptr, 0u) == CompactFormat::R8, "I4 -> R8");
    expect(compact_for_guest(TexFormat::I8, TlutFormat::IA8, 1u, nullptr, 0u) == CompactFormat::R8, "I8 -> R8");
    expect(compact_for_guest(TexFormat::IA4, TlutFormat::IA8, 1u, nullptr, 0u) == CompactFormat::RG8, "IA4 -> RG8");
    expect(compact_for_guest(TexFormat::IA8, TlutFormat::IA8, 1u, nullptr, 0u) == CompactFormat::RG8, "IA8 -> RG8");
    expect(compact_for_guest(TexFormat::RGB565, TlutFormat::IA8, 1u, nullptr, 0u) == CompactFormat::RGB565,
           "RGB565 -> RGB565");
    expect(compact_for_guest(TexFormat::C8, TlutFormat::IA8, 1u, nullptr, 0u) == CompactFormat::RG8, "C8/IA8 -> RG8");
    expect(compact_for_guest(TexFormat::C4, TlutFormat::RGB565, 1u, nullptr, 0u) == CompactFormat::RGB565,
           "C4/RGB565 -> RGB565");
    expect(compact_for_guest(TexFormat::C14X2, TlutFormat::RGB5A3, 1u, nullptr, 0u) == CompactFormat::RGBA8,
           "C14X2/RGB5A3 stays RGBA8 (exact)");
    expect(compact_for_guest(TexFormat::CMPR, TlutFormat::IA8, 1u, nullptr, 0u) == CompactFormat::RGBA8,
           "CMPR stays RGBA8 (exact)");
    expect(compact_for_guest(TexFormat::RGBA8, TlutFormat::IA8, 2u, nullptr, 0u) == CompactFormat::RGBA8,
           "RGBA8 stays RGBA8 (16 bits)");
    expect(compact_for_guest(TexFormat::I8, TlutFormat::IA8, 0u, nullptr, 0u) == CompactFormat::RGBA8, "mode 0: off");
    // Mode 2: by alpha.
    const auto opaque = texels({0x102030FFu, 0xFFFFFFFFu});
    const auto binary = texels({0x10203000u, 0xFFFFFFFFu});
    const auto any = texels({0x10203080u, 0xFFFFFFFFu});
    expect(compact_for_guest(TexFormat::CMPR, TlutFormat::IA8, 2u, opaque.data(), 2u) == CompactFormat::RGB565,
           "CMPR opaque -> RGB565 (16 bits)");
    expect(compact_for_guest(TexFormat::CMPR, TlutFormat::IA8, 2u, binary.data(), 2u) == CompactFormat::RGB5A1,
           "CMPR with holes -> RGB5A1 (16 bits)");
    expect(compact_for_guest(TexFormat::RGB5A3, TlutFormat::IA8, 2u, any.data(), 2u) == CompactFormat::RGBA4,
           "RGB5A3 translucent -> RGBA4 (16 bits)");
    expect(compact_for_guest(TexFormat::C8, TlutFormat::RGB5A3, 2u, binary.data(), 2u) == CompactFormat::RGB5A1,
           "C8/RGB5A3 with holes -> RGB5A1 (16 bits)");
    expect(compact_for_guest(TexFormat::CMPR, TlutFormat::IA8, 2u, nullptr, 0u) == CompactFormat::RGBA8,
           "16 bits without level 0 stays RGBA8");

    // By content (texture packs).
    expect(compact_exact(texels({0x40404040u, 0xFFFFFFFFu}).data(), 2u) == CompactFormat::R8, "grey = alpha -> R8");
    expect(compact_exact(texels({0x40404080u, 0x000000FFu}).data(), 2u) == CompactFormat::RG8, "grey + alpha -> RG8");
    const std::uint32_t c565 = (std::uint32_t{e5(3)} << 24) | (std::uint32_t{e6(40)} << 16) | (std::uint32_t{e5(17)} << 8) | 0xFFu;
    expect(compact_exact(texels({c565, 0xFFFFFFFFu}).data(), 2u) == CompactFormat::RGB565, "565 values -> RGB565");
    const std::uint32_t c5551 = (std::uint32_t{e5(3)} << 24) | (std::uint32_t{e5(20)} << 16) | (std::uint32_t{e5(17)} << 8);
    expect(compact_exact(texels({c5551, 0xFFFFFFFFu}).data(), 2u) == CompactFormat::RGB5A1, "5551 values -> RGB5A1");
    expect(compact_exact(texels({0x11223344u, 0xFFFFFF00u}).data(), 2u) == CompactFormat::RGBA4, "4444 values -> RGBA4");
    expect(compact_exact(texels({0x12345678u}).data(), 1u) == CompactFormat::RGBA8, "anything else -> RGBA8");
    expect(compact_for_replacement(2u, texels({0x123456FFu}).data(), 1u) == CompactFormat::RGB565,
           "replacement, 16 bits: opaque -> RGB565");
    expect(compact_for_replacement(1u, texels({0x123456FFu}).data(), 1u) == CompactFormat::RGBA8,
           "replacement, exact: not 565 values -> RGBA8");

    // A replacement's mip levels in "exacto" (texture_pack.cpp): the format holds every level, not just level 0.
    {
        const std::uint32_t c565o = (std::uint32_t{e5(10)} << 24) | (std::uint32_t{e6(40)} << 16) |
                                    (std::uint32_t{e5(5)} << 8) | 0xFFu;
        const auto base = texels({c565o, c565o, c565o, c565o});        // 2x2, opaque, 565 values
        const auto mip_alpha = texels({(c565o & 0xFFFFFF00u) | 64u});  // 1x1, the same colour with alpha 64
        const std::uint8_t* levels[2] = {base.data(), mip_alpha.data()};
        const std::size_t sizes[2] = {4u, 1u};
        // The fault: level 0 alone picks RGB565, and the mip converted to it loses its alpha.
        const CompactFormat old_choice = compact_for_replacement(1u, base.data(), 4u);
        expect(old_choice == CompactFormat::RGB565, "exacto, level 0 alone: RGB565 (the old choice)");
        std::vector<std::uint8_t> packed(compact_texel_bytes(old_choice)), back(4u);
        compact_convert(old_choice, mip_alpha.data(), 1u, packed.data());
        compact_expand(old_choice, packed.data(), 1u, back.data());
        expect(back[3] != 64u, "the old choice: the mip's alpha 64 was lost (the fault reproduced)");
        // The fix: every level decides; alpha 64 is not a 4-bit value either, so RGBA8, and the mip keeps 64.
        const CompactFormat chosen = compact_for_replacement_levels(1u, levels, sizes, 2u);
        expect(chosen == CompactFormat::RGBA8, "exacto, every level: a mip with alpha 64 -> RGBA8");
        packed.assign(compact_texel_bytes(chosen), 0u);
        compact_convert(chosen, mip_alpha.data(), 1u, packed.data());
        compact_expand(chosen, packed.data(), 1u, back.data());
        expect(back == mip_alpha && back[3] == 64u, "exacto, every level: the mip keeps alpha 64");
        // A 4-bit alpha (68 = 4 * 17) on the mip: RGBA4 holds both levels exactly, and is chosen.
        const std::uint32_t c4 = 0x334455FFu;
        const auto base4 = texels({c4, c4, c4, c4});
        const auto mip4 = texels({(c4 & 0xFFFFFF00u) | 68u});
        const std::uint8_t* levels4[2] = {base4.data(), mip4.data()};
        expect(compact_for_replacement_levels(1u, levels4, sizes, 2u) == CompactFormat::RGBA4,
               "exacto, every level: a 4-bit alpha mip -> RGBA4");
        // Compactable in every level: stays compact (RGB565 for opaque 565 values throughout).
        const auto mip565 = texels({c565o});
        const std::uint8_t* levels565[2] = {base.data(), mip565.data()};
        expect(compact_for_replacement_levels(1u, levels565, sizes, 2u) == CompactFormat::RGB565,
               "exacto, every level 565: stays RGB565");
        const auto grey = texels({0x40404040u, 0x80808080u, 0x40404040u, 0x80808080u});
        const auto grey_mip = texels({0x60606060u});
        const std::uint8_t* levels_grey[2] = {grey.data(), grey_mip.data()};
        expect(compact_for_replacement_levels(1u, levels_grey, sizes, 2u) == CompactFormat::R8,
               "exacto, every level grey = alpha: stays R8");
        // Only "exacto" changes: 16 bits still decides on level 0, off stays off.
        expect(compact_for_replacement_levels(2u, levels, sizes, 2u) == CompactFormat::RGB565,
               "16bits: level 0 decides, as before");
        expect(compact_for_replacement_levels(0u, levels565, sizes, 2u) == CompactFormat::RGBA8, "off: RGBA8");
        expect(compact_exact_levels(levels565, sizes, 1u) == compact_exact(base.data(), 4u),
               "one level: the same as compact_exact");
    }

    // Every value of each width comes back.
    std::vector<std::uint8_t> all;
    for (unsigned v = 0; v < 64u; ++v) {
        all.insert(all.end(), {e5(v & 31u), e6(v), e5(31u - (v & 31u)), 0xFFu});
    }
    expect(round_trip(CompactFormat::RGB565, all), "RGB565 round trip (every 5- and 6-bit value)");
    std::vector<std::uint8_t> all5551;
    for (unsigned v = 0; v < 32u; ++v) {
        all5551.insert(all5551.end(), {e5(v), e5(31u - v), e5((v * 7u) & 31u), (v & 1u) ? std::uint8_t{255} : std::uint8_t{0}});
    }
    expect(round_trip(CompactFormat::RGB5A1, all5551), "RGB5A1 round trip");
    std::vector<std::uint8_t> all4;
    for (unsigned v = 0; v < 16u; ++v) {
        all4.insert(all4.end(), {static_cast<std::uint8_t>(v * 17u), static_cast<std::uint8_t>((15u - v) * 17u),
                                 static_cast<std::uint8_t>(((v * 5u) & 15u) * 17u), static_cast<std::uint8_t>(v * 17u)});
    }
    expect(round_trip(CompactFormat::RGBA4, all4), "RGBA4 round trip (every 4-bit value)");
    std::vector<std::uint8_t> greys;
    for (unsigned v = 0; v < 256u; ++v) {
        const auto g = static_cast<std::uint8_t>(v);
        greys.insert(greys.end(), {g, g, g, static_cast<std::uint8_t>(255u - v)});
    }
    expect(round_trip(CompactFormat::RG8, greys), "RG8 round trip (every intensity and alpha)");
    for (std::size_t i = 0; i < greys.size(); i += 4u) greys[i + 3u] = greys[i];
    expect(round_trip(CompactFormat::R8, greys), "R8 round trip (every intensity)");
    // Rounding, not truncation: 0x7F in 5 bits is 15.5 -> 16 -> 0x84 (error 5, not 0x7B's 4... both within a step).
    std::uint8_t packed[2], back[4];
    const auto mid = texels({0x7F7F7FFFu});
    compact_convert(CompactFormat::RGB565, mid.data(), 1u, packed);
    compact_expand(CompactFormat::RGB565, packed, 1u, back);
    expect(std::abs(int(back[0]) - 0x7F) <= 4 && std::abs(int(back[1]) - 0x7F) <= 2, "RGB565 rounds within half a step");
}

// The texture's level 0 read back as RGBA8 through a framebuffer (raw channels: the swizzle only applies to sampling).
std::vector<std::uint8_t> read_back(GLuint texture, std::uint32_t w, std::uint32_t h) {
    GLuint fb = 0;
    glGenFramebuffers(1, &fb);
    glBindFramebuffer(GL_FRAMEBUFFER, fb);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    std::vector<std::uint8_t> out(static_cast<std::size_t>(w) * h * 4u);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, static_cast<GLsizei>(w), static_cast<GLsizei>(h), GL_RGBA, GL_UNSIGNED_BYTE, out.data());
    } else {
        out.clear();
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fb);
    return out;
}

// What a framebuffer read of a compact texel gives: its stored channels (R8: r 0 0 1; RG8: r g 0 1).
std::vector<std::uint8_t> stored(CompactFormat format, const std::vector<std::uint8_t>& rgba) {
    const std::size_t n = rgba.size() / 4u;
    std::vector<std::uint8_t> packed(n * compact_texel_bytes(format)), out(rgba.size());
    compact_convert(format, rgba.data(), n, packed.data());
    compact_expand(format, packed.data(), n, out.data());
    for (std::size_t i = 0; i < n; ++i) {
        std::uint8_t* p = out.data() + i * 4u;
        if (format == CompactFormat::R8) { p[1] = 0u; p[2] = 0u; p[3] = 255u; }
        if (format == CompactFormat::RG8) { p[1] = p[3]; p[2] = 0u; p[3] = 255u; }
    }
    return out;
}

bool close_enough(const std::vector<std::uint8_t>& got, const std::vector<std::uint8_t>& want) {
    if (got.size() != want.size()) return false;
    for (std::size_t i = 0; i < got.size(); ++i) {
        if (std::abs(int(got[i]) - int(want[i])) > 1) return false;  // the driver's own expansion may differ by one
    }
    return true;
}

void gl_tests() {
    TextureBackend backend;
    if (!backend.initialize(nullptr, 4u)) {
        expect(false, "texture backend initialize");
        return;
    }
    constexpr std::uint32_t w = 8u, h = 4u;
    // Staging as the cache lays it out: RGBA8 rows at a 256-byte pitch; one level.
    const std::uint64_t offsets[1] = {0u};
    const std::uint32_t pitches[1] = {256u};
    std::vector<std::uint8_t> source(static_cast<std::size_t>(w) * h * 4u);
    for (std::uint32_t i = 0; i < w * h; ++i) {
        const auto v = static_cast<std::uint8_t>(i * 8u);
        source[i * 4u + 0u] = v;
        source[i * 4u + 1u] = static_cast<std::uint8_t>(255u - v);
        source[i * 4u + 2u] = static_cast<std::uint8_t>(v ^ 0x5Au);
        source[i * 4u + 3u] = (i & 3u) == 0u ? 0u : 255u;
    }
    for (CompactFormat format : {CompactFormat::R8, CompactFormat::RG8, CompactFormat::RGB565, CompactFormat::RGB5A1,
                                 CompactFormat::RGBA4, CompactFormat::RGBA8}) {
        for (bool already : {false, true}) {
            if (already && format == CompactFormat::RGBA8) continue;
            TextureBackend::UploadChunk chunk = backend.create_upload_chunk(pitches[0] * h);
            const std::uint32_t texel = already ? compact_texel_bytes(format) : 4u;
            for (std::uint32_t row = 0; row < h; ++row) {
                const std::uint8_t* from = source.data() + static_cast<std::size_t>(row) * w * 4u;
                std::uint8_t* to = chunk.cpu_base + static_cast<std::size_t>(row) * pitches[0];
                if (already) compact_convert(format, from, w, to);
                else std::memcpy(to, from, w * texel);
            }
            backend.set_upload_list(true);
            std::uint64_t bytes = 0;
            gpu::TextureRef texture = backend.create_texture(w, h, 1u, bytes, format);
            char what[128];
            std::snprintf(what, sizeof what, "%s made (%llu bytes)", compact_format_name(format),
                          static_cast<unsigned long long>(bytes));
            expect(texture && bytes == std::uint64_t{w} * h * compact_texel_bytes(format), what);
            if (!texture) continue;
            if (already) {
                backend.record_upload_compact(gpu::raw(texture), chunk, 0u, w, h, 1u, offsets, pitches);
            } else {
                backend.record_upload(gpu::raw(texture), false, chunk, 0u, w, h, 1u, offsets, pitches);
            }
            expect(TextureBackend::compact_format(gpu::raw(texture)) == format, "the texture keeps its format");
            const auto got = read_back(gpu::raw(texture)->name, w, h);
            std::snprintf(what, sizeof what, "%s filled from %s staging, read back", compact_format_name(format),
                          already ? "compact" : "RGBA8");
            expect(close_enough(got, stored(format, source)), what);
            expect(glGetError() == GL_NO_ERROR, "no GL error");
            TextureBackend::release_upload_chunk(chunk);
        }
    }
    // Only RGBA8 textures go back to the pool.
    std::uint64_t bytes = 0;
    gpu::TextureRef narrow = backend.create_texture(w, h, 1u, bytes, CompactFormat::RG8);
    std::uint32_t pw = 0, ph = 0, pl = 0;
    expect(narrow && !TextureBackend::poolable(gpu::raw(narrow), pw, ph, pl), "a compact texture is not pooled");
    backend.shutdown();
}

}  // namespace

// --pack <folder>: each texture of a pack as the loader reads it and the format it would go to the GPU in, as
// "<file name> <w>x<h> <format>" lines (Nopal's `nopal texturas` tests check their packs with it). No GL.
int list_pack(const char* folder, unsigned mode) {
    std::vector<std::filesystem::path> files;
    for (const auto& e : std::filesystem::recursive_directory_iterator(folder)) {
        const auto ext = e.path().extension().string();
        if (e.is_regular_file() && (ext == ".png" || ext == ".dds")) files.push_back(e.path());
    }
    std::sort(files.begin(), files.end());
    int failed = 0;
    for (const auto& f : files) {
        TexturePackPixels pixels;
        std::string error;
        if (!load_texture_pack_image(f, f.extension() == ".dds", pixels, &error)) {
            std::printf("%s error %s\n", f.filename().string().c_str(), error.c_str());
            ++failed;
            continue;
        }
        // As the loader decides it (texture_pack.cpp): every level, generated ones included.
        std::vector<const std::uint8_t*> level_data(pixels.levels.size());
        std::vector<std::size_t> level_texels(pixels.levels.size());
        for (std::size_t l = 0; l < pixels.levels.size(); ++l) {
            level_data[l] = pixels.levels[l].data();
            level_texels[l] = static_cast<std::size_t>(std::max(1u, pixels.width >> l)) * std::max(1u, pixels.height >> l);
        }
        const CompactFormat format =
            compact_for_replacement_levels(mode, level_data.data(), level_texels.data(), level_data.size());
        std::printf("%s %ux%u %s\n", f.filename().string().c_str(), pixels.width, pixels.height,
                    compact_format_name(format));
    }
    return failed == 0 ? 0 : 1;
}

int main(int argc, char** argv) {
    if (argc >= 3 && std::strcmp(argv[1], "--pack") == 0) {
        return list_pack(argv[2], argc >= 4 ? static_cast<unsigned>(std::atoi(argv[3])) : 1u);
    }
    cpu_tests();
    setenv("NEBULA_WINDOWED", "1", 0);
    if (!galaxy::sdl::start()) {
        std::printf("FAIL window\n");
        return 2;
    }
    RendererGles renderer;
    if (!renderer.initialize(640, 480, 1u)) {
        std::printf("FAIL renderer initialize\n");
        return 2;
    }
    gl_tests();
    std::printf("texture compact tests: %d failed\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
