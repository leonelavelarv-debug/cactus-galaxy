// SPDX-License-Identifier: GPL-3.0-only
// RendererGles against the live driver through the KNULLI window layer (SDL2; SDL_VIDEODRIVER=offscreen off the
// handheld): EFB clears in rectangles, peeks of colour and depth, an EFB-to-texture copy, the display copy and the
// present, checked for D3D's orientation (the EFB's top row on the screen's top). Exit status 0 when all pass.

#include "sdl_host.h"

#include "galaxy/gx/gles/gles_context.h"
#include "galaxy/gx/gles/pipeline_cache_gles.h"
#include "galaxy/gx/gles/renderer_gles.h"
#include "galaxy/gx/uber_constants.h"
#include "galaxy/gx/vertex_loader.h"
#include "galaxy/gx/efb_copy.h"
#include "galaxy/gx/host_window.h"

#include <SDL.h>

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>

using namespace galaxy::gx;
using namespace galaxy::gx::gles::gl;

namespace {

int g_failures = 0;

void expect(bool ok, const char* what, std::uint32_t got, std::uint32_t want) {
    std::printf("%s %s: got 0x%08x want 0x%08x\n", ok ? "ok  " : "FAIL", what, got, want);
    if (!ok) {
        ++g_failures;
    }
}

std::uint32_t argb(std::uint8_t a, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    return (std::uint32_t{a} << 24u) | (std::uint32_t{r} << 16u) | (std::uint32_t{g} << 8u) | b;
}

EfbCopyParams copy_params(std::uint16_t x, std::uint16_t y, std::uint16_t w, std::uint16_t h, bool xfb) {
    EfbCopyParams p{};
    p.src_x = x;
    p.src_y = y;
    p.src_width = w;
    p.src_height = h;
    p.dest_addr = 0x00400000u;
    p.dest_stride = w * 2u;
    p.y_scale_raw = 256u;
    p.target_format = xfb ? 0u : 6u;  // RGBA8 for the texture copy
    p.copy_to_xfb = xfb;
    p.filter0_raw = (21u << 12u) | (22u << 18u);  // the standard filter: (0, 64, 0)
    p.filter1_raw = 21u;
    return p;
}

}  // namespace

int main() {
    setenv("NEBULA_WINDOWED", "1", 0);
    if (!galaxy::sdl::start()) {
        return 2;
    }
    RendererGles renderer;
    if (!renderer.initialize(640, 480, 1u)) {
        std::printf("FAIL renderer initialize\n");
        return 2;
    }
    renderer.create_first_use_resources();
    PipelineCacheGles pipelines;
    if (!pipelines.initialize(renderer.device(), renderer.root_signature(), "/tmp/claude-0/gles-test-cache", 1u)) {
        std::printf("FAIL pipeline cache initialize\n");
        return 2;
    }
    // The renderer runs on this thread too.
    renderer.begin_frame(true);
    // Black everywhere and farthest; then red above row 264, blue below; depth 0x123456 on the left half.
    renderer.clear_efb(0x0000u, 0x0000u, 0xFFFFFFu, true, true, true);
    renderer.clear_efb(0xFF00u | 0xFFu, 0x0000u, 0u, true, true, false, 0, 0, 640, 264, 1u);
    renderer.clear_efb(0xFF00u, 0x00FFu, 0u, true, true, false, 0, 264, 640, 264, 1u);
    renderer.clear_efb(0u, 0u, 0x123456u, false, false, true, 0, 0, 320, 528, 1u);
    std::uint32_t value = 0;
    (void)renderer.peek_efb(10, 10, EfbPeekKind::Color, value);
    expect(value == argb(0xFF, 0xFF, 0, 0), "peek colour (10,10) red", value, argb(0xFF, 0xFF, 0, 0));
    (void)renderer.peek_efb(10, 500, EfbPeekKind::Color, value);
    expect(value == argb(0xFF, 0, 0, 0xFF), "peek colour (10,500) blue", value, argb(0xFF, 0, 0, 0xFF));
    (void)renderer.peek_efb(10, 10, EfbPeekKind::Depth, value);
    expect(value == 0x123456u, "peek depth (10,10)", value, 0x123456u);
    (void)renderer.peek_efb(600, 10, EfbPeekKind::Depth, value);
    expect(value == 0xFFFFFFu || value == 0xFFFFFEu, "peek depth (600,10) far", value, 0xFFFFFFu);

    // A resolution change keeps the picture and the depth (the dynamic resolution's rebuild): to 50 % and back.
    for (const unsigned percent : {50u, 100u}) {
        const bool resized = renderer.resize_efb_for_tests(percent);
        std::printf("  resized to %u%%: %d\n", percent, resized ? 1 : 0);
        (void)renderer.peek_efb(10, 10, EfbPeekKind::Color, value);
        expect(value == argb(0xFF, 0xFF, 0, 0), "resized peek colour (10,10) red", value, argb(0xFF, 0xFF, 0, 0));
        (void)renderer.peek_efb(10, 500, EfbPeekKind::Color, value);
        expect(value == argb(0xFF, 0, 0, 0xFF), "resized peek colour (10,500) blue", value, argb(0xFF, 0, 0, 0xFF));
        (void)renderer.peek_efb(10, 10, EfbPeekKind::Depth, value);
        expect(value == 0x123456u, "resized peek depth (10,10)", value, 0x123456u);
        (void)renderer.peek_efb(600, 10, EfbPeekKind::Depth, value);
        expect(value >= 0xFFFFFEu, "resized peek depth (600,10) far", value, 0xFFFFFFu);
    }
    // The scaling blurred the red/blue edge (a game frame redraws it): the clears again for the exact checks below.
    renderer.clear_efb(0x0000u, 0x0000u, 0xFFFFFFu, true, true, true);
    renderer.clear_efb(0xFF00u | 0xFFu, 0x0000u, 0u, true, true, false, 0, 0, 640, 264, 1u);
    renderer.clear_efb(0xFF00u, 0x00FFu, 0u, true, true, false, 0, 264, 640, 264, 1u);
    renderer.clear_efb(0u, 0u, 0x123456u, false, false, true, 0, 0, 320, 528, 1u);

    // The pointer's depth field: the whole EFB at once.
    std::vector<std::uint32_t> field(kEfbWidth * kEfbHeight);
    if (renderer.capture_pointer_depth(field)) {
        expect(field[10u * kEfbWidth + 10u] == 0x123456u, "depth field (10,10)", field[10u * kEfbWidth + 10u],
               0x123456u);
        expect(field[500u * kEfbWidth + 600u] >= 0xFFFFFEu, "depth field (600,500)", field[500u * kEfbWidth + 600u],
               0xFFFFFFu);
        // Both sides of the clears' edges (a divided EFB expands each physical texel on the CPU).
        expect(field[263u * kEfbWidth + 319u] == 0x123456u, "depth field (319,263)", field[263u * kEfbWidth + 319u],
               0x123456u);
        expect(field[527u * kEfbWidth + 0u] == 0x123456u, "depth field (0,527)", field[527u * kEfbWidth], 0x123456u);
        expect(field[264u * kEfbWidth + 320u] >= 0xFFFFFEu, "depth field (320,264)", field[264u * kEfbWidth + 320u],
               0xFFFFFFu);
        expect(field[527u * kEfbWidth + 639u] >= 0xFFFFFEu, "depth field (639,527)", field[527u * kEfbWidth + 639u],
               0xFFFFFFu);
    } else {
        expect(false, "depth field captured", 0, 1);
    }

    // An async colour field (the game's later GXPeekARGB answers): red above row 264, blue from it.
    {
        const std::uint64_t fence = renderer.submit_async_peek_field(0u, EfbPeekKind::Color, 1u);
        expect(fence != 0u, "async colour field submitted", fence != 0u, 1u);
        if (fence != 0u) {
            (void)galaxy::gx::gles::Context::get().wait(fence, UINT64_MAX);
            const bool completed = renderer.try_complete_async_peek_field(0u);
            expect(completed, "async colour field completed", completed ? 1u : 0u, 1u);
            const std::uint8_t* rgba = renderer.async_peek_field_rgba(0u, 1u);
            // As GxBackend reads it: a reduced field (GALAXY_GLES_PEEK_FIELD) keeps its texels, logical x at x*num/den.
            unsigned fw = 0, fh = 0, num = 1, den = 1;
            renderer.async_peek_field_layout(0u, 1u, fw, fh, num, den);
            std::printf("async field layout %ux%u, %u/%u\n", fw, fh, num, den);
            const auto pixel = [&](unsigned x, unsigned y) {
                const std::size_t tx = std::min<std::size_t>(static_cast<std::size_t>(x) * num / den, fw - 1u);
                const std::size_t ty = std::min<std::size_t>(static_cast<std::size_t>(y) * num / den, fh - 1u);
                const std::uint8_t* p = rgba + (ty * fw + tx) * 4u;
                return argb(p[3], p[0], p[1], p[2]);
            };
            if (rgba != nullptr) {
                expect(pixel(10, 263) == argb(0xFF, 0xFF, 0, 0), "async field (10,263) red", pixel(10, 263),
                       argb(0xFF, 0xFF, 0, 0));
                expect(pixel(639, 0) == argb(0xFF, 0xFF, 0, 0), "async field (639,0) red", pixel(639, 0),
                       argb(0xFF, 0xFF, 0, 0));
                expect(pixel(11, 264) == argb(0xFF, 0, 0, 0xFF), "async field (11,264) blue", pixel(11, 264),
                       argb(0xFF, 0, 0, 0xFF));
                expect(pixel(639, 527) == argb(0xFF, 0, 0, 0xFF), "async field (639,527) blue", pixel(639, 527),
                       argb(0xFF, 0, 0, 0xFF));
            }
        }
    }

    // The display copy and the present: red at the screen's top.
    XfbTexture xfb{};
    xfb.texture = galaxy::gx::gpu::create_render_texture(renderer.device(), 640u, 528u);
    xfb.width = 640u;
    xfb.height = 528u;
    renderer.copy_efb_to_xfb(copy_params(0, 0, 640, 528, true), xfb);
    renderer.present(&xfb, false, false);
    // The window as presented (before the swap): GL's rows from the bottom.
    int w = 0, h = 0;
    host_window::gles_drawable_size(galaxy::sdl::input_state().window_width ? nullptr : nullptr, w, h);
    w = static_cast<int>(host_window::backbuffer_width);
    h = static_cast<int>(host_window::backbuffer_height);
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(w) * h * 4u);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    const auto at = [&](int x, int y_from_top) {
        const std::uint8_t* p = pixels.data() + (static_cast<std::size_t>(h - 1 - y_from_top) * w + x) * 4u;
        return argb(p[3], p[0], p[1], p[2]);
    };
    const std::uint32_t top = at(w / 2, h / 4);
    const std::uint32_t bottom = at(w / 2, h - h / 4);
    expect((top & 0xFFFFFFu) == 0xFF0000u, "screen top red", top, 0xFFFF0000u);
    expect((bottom & 0xFFFFFFu) == 0x0000FFu, "screen bottom blue", bottom, 0xFF0000FFu);
    for (int y = 0; y < h; y += h / 16) {
        std::printf("  row %4d: %08x\n", y, at(w / 2, y));
    }
    // The red/blue edge up close (the display copy's upscale and sharpen).
    for (int y = h / 2 - 5; y <= h / 2 + 5; ++y) {
        std::printf("  edge row %4d: %08x\n", y, at(w / 2, y));
    }
    renderer.end_frame(true);
    renderer.begin_frame(true);

    // A GX draw: the inline position matrix and projection identity, one triangle over the EFB's top-left quarter
    // in D3D's clip space ((-1, 1), (0, 1), (-1, 0): clockwise on the screen, D3D's front, GX's back) at z 0.25, depth
    // written always. Drawn with GX cull Front, culled with cull Back; the depth field shows where.
    // With `palette_x` the position matrix comes from the matrix palette (vertex shader flag 1: rows 0-2, an identity
    // moved by palette_x in clip space), so the palette's binding is read (OpenGL ES 3.0: its 2D texture).
    // The default pixel key's alpha test never passes: its depth is GX's early Z's (written before the alpha test
    // discards). GLSL ES 3.00 has no early_fragment_tests (docs/GX-ES.md), so on OpenGL ES 3.0 the draws pass the
    // alpha test instead (GX_ALWAYS = 7) and the early-Z case is reported as not testable there.
    const bool es30 = gles::Context::get().caps().glsl_version == 300;
    if (es30) {
        std::printf("skip GX early Z with a failing alpha test: GLSL ES 3.00 has no early_fragment_tests\n");
    }
    const auto draw_triangle = [&](CullMode cull, float palette_x = 0.0f, bool use_palette = false,
                                   bool indexed = false) {
        renderer.clear_efb(0u, 0u, 0xFFFFFFu, true, true, true);
        VertexShaderKey vs_key{};
        PixelShaderKey ps_key{};
        if (use_palette) {
            vs_key.flags |= 0x01u;
        }
        if (use_palette || es30) {
            ps_key.alpha_comp0 = 7u;
            ps_key.alpha_comp1 = 7u;
        }
        RenderStateKey rs{};
        rs.zmode_bits = 1u | (7u << 1u) | (1u << 4u);
        rs.blend_bits = (1u << 25u) | (1u << 26u);
        rs.cull = static_cast<std::uint8_t>(cull);
        rs.pixfmt = 1u;
        rs.primitive_topology = gpu::kTopologyTypeTriangle;
        const PsoKey key{vs_key.hash(), ps_key.hash(), rs};
        DrawCall call{};
        call.pipeline = pipelines.get(key, ps_key, vs_key);
        if (call.pipeline == nullptr) {
            expect(false, "pipeline built", 0, 1);
            return;
        }
        UploadRing::Allocation vs = renderer.constant_ring().allocate(sizeof(GxVsConstants), 256u);
        auto* constants = reinterpret_cast<GxVsConstants*>(vs.cpu);
        std::memset(constants, 0, sizeof(GxVsConstants));
        for (int i = 0; i < 4; ++i) constants->projection[i][i] = 1.0f;
        for (int i = 0; i < 3; ++i) constants->inline_pos_matrix[i][i] = 1.0f;
        UploadRing::Allocation ps = renderer.constant_ring().allocate(sizeof(GxPsConstants), 256u);
        std::memset(ps.cpu, 0, sizeof(GxPsConstants));
        UploadRing::Allocation palette = renderer.matrix_ring().allocate(GxState::kMatrixPaletteWords * 4u, 256u);
        std::memset(palette.cpu, 0, GxState::kMatrixPaletteWords * 4u);
        if (use_palette) {
            auto* rows = reinterpret_cast<float*>(palette.cpu);
            for (int r = 0; r < 3; ++r) rows[r * 4 + r] = 1.0f;
            rows[0 * 4 + 3] = palette_x;
        }
        UploadRing::Allocation vertices = renderer.vertex_ring().allocate(3u * sizeof(GxVertexOut), 16u);
        auto* v = reinterpret_cast<GxVertexOut*>(vertices.cpu);
        std::memset(v, 0, 3u * sizeof(GxVertexOut));
        const float positions[3][3] = {{-1.0f, 1.0f, 0.25f}, {0.0f, 1.0f, 0.25f}, {-1.0f, 0.0f, 0.25f}};
        for (int i = 0; i < 3; ++i) std::memcpy(reinterpret_cast<std::byte*>(&v[i]), positions[i], 12u);
        call.vs_constants = vs.gpu;
        call.ps_constants = ps.gpu;
        call.matrix_palette = palette.gpu;
        call.vertex_view = gpu::vertex_view(vertices.gpu, 3u * sizeof(GxVertexOut), sizeof(GxVertexOut));
        call.index_count = 3u;
        if (indexed) {
            // Through the index ring (glDrawRangeElements 0..2), in the same winding.
            UploadRing::Allocation ib = renderer.index_ring().allocate(3u * sizeof(std::uint16_t), 2u);
            auto* index = reinterpret_cast<std::uint16_t*>(ib.cpu);
            index[0] = 0u;
            index[1] = 1u;
            index[2] = 2u;
            call.index_view = gpu::index_view(ib.gpu, 3u * sizeof(std::uint16_t));
        }
        call.topology = gpu::kTopologyTriangleList;
        call.scissor = gpu::rect(0, 0, 640, 528);
        renderer.bind_efb(nullptr);
        renderer.draw(call);
    };
    draw_triangle(CullMode::Front);
    (void)renderer.peek_efb(10, 10, EfbPeekKind::Depth, value);
    expect(value == 0xC00000u, "triangle covers (10,10) at z 0.25", value, 0xC00000u);
    (void)renderer.peek_efb(630, 10, EfbPeekKind::Depth, value);
    expect(value == 0xFFFFFFu, "triangle leaves (630,10)", value, 0xFFFFFFu);
    (void)renderer.peek_efb(10, 520, EfbPeekKind::Depth, value);
    expect(value == 0xFFFFFFu, "triangle leaves (10,520)", value, 0xFFFFFFu);
    (void)renderer.peek_efb(300, 240, EfbPeekKind::Depth, value);
    expect(value == 0xFFFFFFu, "triangle leaves (300,240), past its diagonal", value, 0xFFFFFFu);
    draw_triangle(CullMode::Back);
    (void)renderer.peek_efb(10, 10, EfbPeekKind::Depth, value);
    expect(value == 0xFFFFFFu, "GX back face culled (cull Back)", value, 0xFFFFFFu);
    // The same triangle indexed (glDrawRangeElements): covered, and culled the same.
    draw_triangle(CullMode::Front, 0.0f, false, true);
    (void)renderer.peek_efb(10, 10, EfbPeekKind::Depth, value);
    expect(value == 0xC00000u, "indexed triangle covers (10,10)", value, 0xC00000u);
    (void)renderer.peek_efb(300, 240, EfbPeekKind::Depth, value);
    expect(value == 0xFFFFFFu, "indexed triangle leaves (300,240)", value, 0xFFFFFFu);
    draw_triangle(CullMode::Back, 0.0f, false, true);
    (void)renderer.peek_efb(10, 10, EfbPeekKind::Depth, value);
    expect(value == 0xFFFFFFu, "indexed triangle culled (cull Back)", value, 0xFFFFFFu);
    // The palette's matrix: moved right by 1 (half the clip space), then back with a new palette in the same ring.
    draw_triangle(CullMode::Front, 1.0f, true);
    (void)renderer.peek_efb(330, 10, EfbPeekKind::Depth, value);
    expect(value == 0xC00000u, "palette matrix moves the triangle to (330,10)", value, 0xC00000u);
    (void)renderer.peek_efb(10, 10, EfbPeekKind::Depth, value);
    expect(value == 0xFFFFFFu, "and off (10,10)", value, 0xFFFFFFu);
    draw_triangle(CullMode::Front, 0.0f, true);
    (void)renderer.peek_efb(10, 10, EfbPeekKind::Depth, value);
    expect(value == 0xC00000u, "a second palette puts it back at (10,10)", value, 0xC00000u);
    renderer.end_frame(true);

    // A second frame: the slots and fences come round.
    renderer.begin_frame(true);
    renderer.clear_efb(0xFF00u | 0x40u, 0x8000u, 0xFFFFFFu, true, true, true);
    (void)renderer.peek_efb(320, 264, EfbPeekKind::Color, value);
    expect(value == argb(0xFF, 0x40, 0x80, 0x00), "frame 2 peek", value, argb(0xFF, 0x40, 0x80, 0x00));
    renderer.end_frame(true);
    renderer.begin_frame(true);
    renderer.end_frame(true);

    // A frame that fills the vertex ring's segment continues in the next slot (a galaxy's loading frame passed
    // 28 MiB on the console and stopped the game); the frames after it come round every slot as usual.
    {
        UploadRing& ring = renderer.vertex_ring();
        const std::size_t per_frame = ring.bytes_per_frame();
        renderer.begin_frame(true);
        const std::uint64_t before = ring.overflows();
        (void)ring.allocate(per_frame - 256u, 16u);
        // What a draw batch asks before appending (GxBackend::batch_rings_fit).
        expect(ring.fits(240u, 16u), "240 bytes still fit", 0u, 1u);
        expect(!ring.fits(432u, 16u), "432 bytes do not", 1u, 0u);
        draw_triangle(CullMode::Front);  // its 432 bytes do not fit in what is left
        expect(ring.overflows() == before + 1u, "vertex ring continued in the next slot",
               static_cast<std::uint32_t>(ring.overflows() - before), 1u);
        expect(ring.borrowed_slots() == 1u, "one slot borrowed", ring.borrowed_slots(), 1u);
        (void)renderer.peek_efb(10, 10, EfbPeekKind::Depth, value);
        expect(value == 0xC00000u, "the triangle past the segment is drawn", value, 0xC00000u);
        renderer.end_frame(true);
        for (unsigned i = 0; i < kFramesInFlight + 1u; ++i) {
            renderer.begin_frame(true);
            draw_triangle(CullMode::Front);
            (void)renderer.peek_efb(10, 10, EfbPeekKind::Depth, value);
            expect(value == 0xC00000u, "frames after the overflow draw", value, 0xC00000u);
            expect(ring.borrowed_slots() == 0u, "and borrow nothing", ring.borrowed_slots(), 0u);
            renderer.end_frame(true);
        }
        // More than every slot together is still fatal: geometry is never dropped silently.
        renderer.begin_frame(true);
        bool threw = false;
        try {
            for (unsigned i = 0; i <= kFramesInFlight; ++i) {
                (void)ring.allocate(per_frame - 64u, 16u);
            }
        } catch (const std::runtime_error&) {
            threw = true;
        }
        expect(threw, "a frame past every slot stops", threw ? 1u : 0u, 1u);
        expect(ring.borrowed_slots() == kFramesInFlight - 1u, "after borrowing the other slots",
               ring.borrowed_slots(), kFramesInFlight - 1u);
        renderer.end_frame(true);
        renderer.begin_frame(true);
        draw_triangle(CullMode::Front);
        (void)renderer.peek_efb(10, 10, EfbPeekKind::Depth, value);
        expect(value == 0xC00000u, "and the next frame draws", value, 0xC00000u);
        renderer.end_frame(true);
    }

    // GALAXY_GLES_SLOT_POR_IMAGEN: stretches without a present share the slot (nothing rewound, no wait), a present
    // or kMaxStretchesPerSlot stretches move to the next slot, and what earlier stretches wrote is left alone.
    {
        renderer.set_slot_per_present_for_tests(true);
        UploadRing& constants = renderer.constant_ring();
        renderer.begin_frame(true);
        expect(renderer.slot_rotated(), "slot per picture: the first stretch takes a slot", 0u, 1u);
        const unsigned first_slot = renderer.frame_slot();
        const UploadRing::Allocation a = constants.allocate(256u, 16u);
        std::memset(a.cpu, 0x5A, 256u);
        draw_triangle(CullMode::Front);
        renderer.end_frame(true);  // no present
        renderer.begin_frame(true);
        expect(!renderer.slot_rotated(), "a stretch without a present before it keeps the slot", 1u, 0u);
        expect(renderer.frame_slot() == first_slot, "the same slot", renderer.frame_slot(), first_slot);
        const UploadRing::Allocation b = constants.allocate(256u, 16u);
        expect(b.offset >= a.offset + 256u, "its uploads go after the earlier stretch's (no rewind)",
               static_cast<std::uint32_t>(b.offset - a.offset), 256u);
        bool kept = true;
        for (unsigned i = 0; i < 256u; ++i) {
            kept &= a.cpu[i] == std::byte{0x5A};
        }
        expect(kept, "the earlier stretch's upload is intact", kept ? 1u : 0u, 1u);
        draw_triangle(CullMode::Front);
        (void)renderer.peek_efb(10, 10, EfbPeekKind::Depth, value);
        expect(value == 0xC00000u, "a continued stretch draws", value, 0xC00000u);
        XfbTexture out{};
        out.texture = galaxy::gx::gpu::create_render_texture(renderer.device(), 640u, 528u);
        out.width = 640u;
        out.height = 528u;
        renderer.copy_efb_to_xfb(copy_params(0, 0, 640, 528, true), out);
        renderer.present(&out, false, false);
        renderer.end_frame(true);
        renderer.begin_frame(true);
        expect(renderer.slot_rotated(), "after a present the next stretch takes a new slot", 0u, 1u);
        expect(renderer.frame_slot() != first_slot, "a different slot", renderer.frame_slot(), (first_slot + 1u) % kFramesInFlight);
        draw_triangle(CullMode::Front);
        (void)renderer.peek_efb(10, 10, EfbPeekKind::Depth, value);
        expect(value == 0xC00000u, "and draws", value, 0xC00000u);
        renderer.end_frame(true);
        unsigned rotations = 0;
        for (unsigned i = 0; i < RendererGles::kMaxStretchesPerSlot; ++i) {
            renderer.begin_frame(true);
            rotations += renderer.slot_rotated() ? 1u : 0u;
            renderer.end_frame(true);
        }
        expect(rotations == 1u, "without presents a slot takes at most kMaxStretchesPerSlot stretches", rotations, 1u);
        // Every slot used and come round again (each wait passes; GL errors are checked below by the context).
        for (unsigned i = 0; i < 3u * kFramesInFlight; ++i) {
            renderer.begin_frame(true);
            draw_triangle(CullMode::Front);
            if (i % 2u == 1u) {
                renderer.copy_efb_to_xfb(copy_params(0, 0, 640, 528, true), out);
                renderer.present(&out, false, false);
            }
            renderer.end_frame(true);
        }
        renderer.begin_frame(true);
        draw_triangle(CullMode::Front);
        (void)renderer.peek_efb(10, 10, EfbPeekKind::Depth, value);
        expect(value == 0xC00000u, "slots per picture: later frames draw", value, 0xC00000u);
        expect(glGetError() == GL_NO_ERROR, "slots per picture: no GL error", 0u, 0u);
        renderer.end_frame(true);
        renderer.set_slot_per_present_for_tests(false);
    }

    // GLES_TEST_EDGE_PPM=<file>: a white diagonal on black through the display copy and present, the window saved
    // (to look at the upscale's filters by eye).
    if (const char* edge_ppm = std::getenv("GLES_TEST_EDGE_PPM"); edge_ppm != nullptr) {
        renderer.begin_frame(true);
        renderer.clear_efb(0xFF00u, 0x0000u, 0xFFFFFFu, true, true, true);
        for (unsigned y = 0; y < 528u; ++y) {
            renderer.clear_efb(0xFFFFu, 0xFFFFu, 0u, true, true, false, 0, y, std::min(640u, 160u + y * 3u / 4u), 1u,
                               1u);
        }
        XfbTexture out{};
        out.texture = galaxy::gx::gpu::create_render_texture(renderer.device(), 640u, 528u);
        out.width = 640u;
        out.height = 528u;
        renderer.copy_efb_to_xfb(copy_params(0, 0, 640, 528, true), out);
        renderer.present(&out, false, false);
        const int ew = static_cast<int>(host_window::backbuffer_width);
        const int eh = static_cast<int>(host_window::backbuffer_height);
        std::vector<std::uint8_t> rgba(static_cast<std::size_t>(ew) * eh * 4u);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, ew, eh, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        if (FILE* f = std::fopen(edge_ppm, "wb")) {
            std::fprintf(f, "P6\n%d %d\n255\n", ew, eh);
            for (int y = eh - 1; y >= 0; --y) {
                for (int x = 0; x < ew; ++x) {
                    std::fwrite(rgba.data() + (static_cast<std::size_t>(y) * ew + x) * 4u, 1, 3, f);
                }
            }
            std::fclose(f);
        }
        renderer.end_frame(true);
    }
    // GALAXY_GLES_COPIA_REDUCIDA: a 640x456 colour copy written at 3/4 (480x342) samples one logical source pixel
    // per 3/4 of a destination texel, so one physical EFB texel per destination texel (step 1.0 after to_physical),
    // from the same origin; the copy filter's taps stay one logical row apart (0.75 physical texels).
    {
        EfbCopyParams p = copy_params(0, 0, 640, 456, false);
        p.src_x = 32;
        p.src_y = 16;
        const auto full = galaxy::gx::compute_efb_copy_sampling_geometry(p, 640u, 456u, 1u);
        const auto reduced = galaxy::gx::compute_efb_copy_sampling_geometry(p, 480u, 342u, 1u);
        const auto bits = [](float v) {
            std::uint32_t b = 0;
            std::memcpy(&b, &v, sizeof(b));
            return b;
        };
        expect(reduced.src_x == full.src_x && reduced.src_y == full.src_y, "reduced copy origin", bits(reduced.src_x),
               bits(full.src_x));
        expect(reduced.step_x * 0.75f == 1.0f && reduced.step_y * 0.75f == 1.0f, "reduced copy one texel a texel",
               bits(reduced.step_x * 0.75f), bits(1.0f));
        expect(full.step_x * 0.75f == 0.75f, "full copy 3/4 texel a texel", bits(full.step_x * 0.75f), bits(0.75f));
        expect(reduced.filter_row_offset == full.filter_row_offset && reduced.filter_row_offset == 1.0f,
               "reduced copy filter taps one logical row", bits(reduced.filter_row_offset), bits(1.0f));
    }
    pipelines.shutdown();
    renderer.shutdown();
    std::printf("gles renderer tests: %d failed\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
