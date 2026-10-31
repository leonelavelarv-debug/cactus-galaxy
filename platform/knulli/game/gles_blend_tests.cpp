// SPDX-License-Identifier: GPL-3.0-only
// GX blending with the TEV's alpha (dual source) on OpenGL ES: docs/GX_ES_BLEND_ANALYSIS.md, GX_ES_VISUAL_REGRESSION.md.
//
//   nebula_gles_blend_tests --modo <plain|fetch|referencia> <carpeta>
//       draws the scenes and writes <carpeta>/<modo>/<escena>.png (the EFB, RGBA) and fondo-<escena>.png (the EFB
//       before the scene's blended draws). plain: GALAXY_GLES_BLEND_FETCH=0 (fixed-function blending, the backend
//       before step 2); fetch: blending in the shader with framebuffer fetch; referencia: the dual-source pipelines
//       blend with GL_EXT_blend_func_extended (SRC1_ALPHA), as D3D12 and Vulkan do (the driver must have it).
//       Every pixel of the exact scenes is also checked against GX's blend computed here (escena.json gives the
//       tiles); the exit status says whether the mode met its expectation (see main).
//   nebula_gles_blend_tests --comparar <carpeta>
//       the three modes against each other and against the computed blend: diff images (<carpeta>/dif/) and
//       <carpeta>/metricas.txt (per scene: pixels compared, % different, mean and max absolute error per channel).
//
// Scenes (640x528, the EFB at scale 1):
//   exactas-<blend>  tiles of flat colour and alpha over a gradient (colour and alpha) background, one GX blend each:
//                    the five dual-source factor pairs Galaxy uses (vk_seed_keys.bin), with and without
//                    GXSetDstAlpha and the EFB alpha write; and controls that are not dual source.
//   gradientes       the same blends with alpha 0-255 across each tile and colour down it (the visual check).
//   superpuestas     four overlapping quads of different blends and alphas (several transparencies).
//   claves-reales    every dual-source pipeline of the whole tour's keys (241), one tile each, with the real
//                    vertex and pixel shaders and render state, over the gradient.
//   controles-reales 256 pipelines of the tour that are not dual source, the same way: must not change.
#include "sdl_host.h"

#include "galaxy/gx/gles/gles_context.h"
#include "galaxy/gx/gles/pipeline_cache_gles.h"
#include "galaxy/gx/gles/renderer_gles.h"
#include "galaxy/gx/gles/shader_library_gles.h"
#include "galaxy/gx/gles/shader_sources_gles.h"
#include "galaxy/gx/uber_constants.h"
#include "galaxy/gx/vertex_loader.h"
#include "galaxy/gx/host_window.h"

#define STBI_NO_STDIO
#include "../../../third_party/stb/stb_image.h"
#define STBI_WRITE_NO_STDIO
#include "../../../third_party/stb/stb_image_write.h"

#include <SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

using namespace galaxy::gx;
using namespace galaxy::gx::gles::gl;

namespace {

constexpr int kW = 640;
constexpr int kH = 528;

struct Image {
    int w = kW, h = kH;
    std::vector<std::uint8_t> rgba;  // top row first (the EFB's orientation)
};

bool write_png(const std::filesystem::path& path, const Image& image) {
    std::vector<std::uint8_t> out;
    const auto sink = [](void* context, void* data, int size) {
        auto* bytes = static_cast<std::vector<std::uint8_t>*>(context);
        bytes->insert(bytes->end(), static_cast<std::uint8_t*>(data), static_cast<std::uint8_t*>(data) + size);
    };
    if (stbi_write_png_to_func(sink, &out, image.w, image.h, 4, image.rgba.data(), image.w * 4) == 0) {
        return false;
    }
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
    return static_cast<bool>(file);
}

bool read_png(const std::filesystem::path& path, Image& image) {
    std::ifstream file(path, std::ios::binary);
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    int w = 0, h = 0, n = 0;
    std::uint8_t* pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &n, 4);
    if (pixels == nullptr) {
        return false;
    }
    image.w = w;
    image.h = h;
    image.rgba.assign(pixels, pixels + static_cast<std::size_t>(w) * h * 4u);
    stbi_image_free(pixels);
    return true;
}

// ---- GX blend, computed here (GX semantics as D3D12/Vulkan draw it: the colour factors read the TEV alpha,
// SRC1_ALPHA; the alpha factors read location 0's alpha).

struct Blend {
    std::uint32_t src = 4, dst = 5;  // GX BlendFactor
    bool dual = true;                // RGBA6 EFB and a factor that reads the source alpha
    bool dst_alpha = false;          // GXSetDstAlpha (constant alpha into the EFB)
    bool alpha_write = false;
    std::uint8_t pixfmt = 1;         // 1 RGBA6_Z24, 0 RGB8_Z24
    const char* name = "";
};

// One flat tile: colour, TEV alpha and GXSetDstAlpha's constant (8-bit values).
struct Tile {
    int x0, y0, x1, y1;
    std::uint8_t r, g, b, a, k;
    Blend blend;
};

double colour_factor(std::uint32_t f, bool source, double s, double d, double t, double da) {
    switch (f & 7u) {
    case 0: return 0.0;
    case 1: return 1.0;
    case 2: return source ? d : s;
    case 3: return source ? 1.0 - d : 1.0 - s;
    case 4: return t;
    case 5: return 1.0 - t;
    case 6: return da;
    default: return 1.0 - da;
    }
}

double alpha_factor(std::uint32_t f, bool source, double sa, double da) {
    switch (f & 7u) {
    case 0: return 0.0;
    case 1: return 1.0;
    case 2: return source ? da : sa;
    case 3: return source ? 1.0 - da : 1.0 - sa;
    case 4: return sa;
    case 5: return 1.0 - sa;
    case 6: return da;
    default: return 1.0 - da;
    }
}

// The EFB pixel after a tile's draw over `dst` (bytes). `tev_alpha_for_colour` false: what fixed-function blending
// with location 0's alpha gives (the backend before step 2), to show the error it makes.
std::array<std::uint8_t, 4> gx_blend(const Tile& tile, const std::uint8_t* dst, bool tev_alpha_for_colour) {
    const Blend& b = tile.blend;
    // The pixel shader's outputs (shader_gen.cpp): colour at 8 bits (RGBA6 with 8-bit colour, the default), alpha
    // at 6 bits; GXSetDstAlpha's constant in place of the alpha; a TEV alpha of 1 counts as 0.
    const std::uint8_t a8 = tile.a == 1u ? 0u : tile.a;
    const double t = a8 / 255.0;
    const double o = b.dst_alpha ? (tile.k >> 2u) / 63.0 : (a8 >> 2u) / 63.0;
    const double s[3] = {tile.r / 255.0, tile.g / 255.0, tile.b / 255.0};
    const double d[4] = {dst[0] / 255.0, dst[1] / 255.0, dst[2] / 255.0, dst[3] / 255.0};
    // The EFB alpha the blend reads: RGBA6 stores it; RGB8 reads one (pipeline_cache_gles.cpp no_dst_alpha).
    const double da = b.pixfmt == 1u ? d[3] : 1.0;
    const double tc = b.dual && tev_alpha_for_colour ? t : o;
    std::array<std::uint8_t, 4> out{};
    for (int c = 0; c < 3; ++c) {
        const double v = s[c] * colour_factor(b.src, true, s[c], d[c], tc, da) +
            d[c] * colour_factor(b.dst, false, s[c], d[c], tc, da);
        out[static_cast<std::size_t>(c)] = static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0, 1.0) * 255.0));
    }
    if (!b.alpha_write) {
        out[3] = dst[3];
    } else if (b.dst_alpha) {
        out[3] = static_cast<std::uint8_t>(std::lround(o * 255.0));
    } else {
        const double v = o * alpha_factor(b.src, true, o, da) + d[3] * alpha_factor(b.dst, false, o, da);
        out[3] = static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0, 1.0) * 255.0));
    }
    return out;
}

// ---- Drawing.

// The keys of a draw whose TEV output is its vertex colour (one stage: colour RASC, alpha RASA; channel 0's
// material from the vertex, unlit).
void passthrough_keys(const Blend& b, VertexShaderKey& vs, PixelShaderKey& ps, RenderStateKey& rs) {
    vs = VertexShaderKey{};
    vs.num_chans = 1;
    vs.channel[0] = 1u;  // MatSrc vertex, no lighting
    vs.channel[2] = 1u;
    ps = PixelShaderKey{};
    ps.num_tev_stages = 1;
    ps.num_chans = 1;
    ps.alpha_comp0 = 7;  // ALWAYS
    ps.alpha_comp1 = 7;
    ps.flags = static_cast<std::uint8_t>((b.dual ? 0x80u : 0u) | (b.dst_alpha ? 0x04u : 0u));
    ps.output_flags = static_cast<std::uint8_t>(b.pixfmt == 1u ? (1u | 0x10u) : 0u);
    const std::uint32_t colour = 10u | (15u << 4u) | (15u << 8u) | (15u << 12u) | (1u << 19u);  // d = RASC, clamp
    const std::uint32_t alpha = (5u << 4u) | (7u << 7u) | (7u << 10u) | (7u << 13u) | (1u << 19u);  // d = RASA
    const std::uint32_t identity = (0u << 10u) | (1u << 12u) | (2u << 14u) | (3u << 16u) |
        (0u << 18u) | (1u << 20u) | (2u << 22u) | (3u << 24u);
    ps.stages[0] = TevStagePacked{colour, alpha, 0u, identity};
    rs = RenderStateKey{};
    rs.blend_bits = 1u | ((b.src & 7u) << 2u) | ((b.dst & 7u) << 5u) | (1u << 25u) |
        (b.alpha_write && b.pixfmt == 1u ? (1u << 26u) : 0u) | (b.dual ? (1u << 27u) : 0u) |
        (b.dst_alpha ? (1u << 28u) : 0u);
    rs.pixfmt = b.pixfmt;
    rs.primitive_topology = gpu::kTopologyTypeTriangle;
}

struct KeyRecord {
    PsoKey key{};
    VertexShaderKey vs_key{};
    PixelShaderKey ps_key{};
};
static_assert(sizeof(KeyRecord) == 464);

std::vector<KeyRecord> read_keys(const char* path) {
    std::vector<KeyRecord> keys;
    std::ifstream in(path, std::ios::binary);
    std::uint32_t magic = 0, version = 0, count = 0;
    in.read(reinterpret_cast<char*>(&magic), 4);
    in.read(reinterpret_cast<char*>(&version), 4);
    in.read(reinterpret_cast<char*>(&count), 4);
    for (std::uint32_t i = 0; i < count && in; ++i) {
        KeyRecord record;
        if (in.read(reinterpret_cast<char*>(&record), sizeof(record))) {
            keys.push_back(record);
        }
    }
    return keys;
}

class Scenes {
public:
    Scenes(RendererGles& renderer, PipelineCacheGles& pipelines, std::string mode)
        : renderer_(renderer), pipelines_(pipelines), mode_(std::move(mode)) {}

    // The EFB as it is now.
    Image read_efb() {
        Image image;
        image.rgba.resize(static_cast<std::size_t>(kW) * kH * 4u);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, renderer_.efb_framebuffer_for_tests());
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, kW, kH, GL_RGBA, GL_UNSIGNED_BYTE, image.rgba.data());
        // GL row 0 is the EFB's top row (the EFB is drawn as D3D draws: renderer_gles.h): already top first.
        return image;
    }

    // A quad from EFB pixel (x0, y0) to (x1, y1) with colours at its corners (0xRRGGBBAA): TL, TR, BL, BR.
    void quad(gles::Pipeline* pipeline, int x0, int y0, int x1, int y1, const std::array<std::uint32_t, 4>& colours,
              const GxVsConstants* vs_override = nullptr, const GxPsConstants* ps_override = nullptr,
              float dst_alpha = 0.0f) {
        if (pipeline == nullptr) {
            ++missing_pipelines_;
            return;
        }
        UploadRing::Allocation vs = renderer_.constant_ring().allocate(sizeof(GxVsConstants), 256u);
        auto* vc = reinterpret_cast<GxVsConstants*>(vs.cpu);
        if (vs_override != nullptr) {
            std::memcpy(vc, vs_override, sizeof(GxVsConstants));
        } else {
            std::memset(vc, 0, sizeof(GxVsConstants));
            for (int i = 0; i < 4; ++i) vc->projection[i][i] = 1.0f;
            for (int i = 0; i < 3; ++i) vc->inline_pos_matrix[i][i] = 1.0f;
        }
        UploadRing::Allocation ps = renderer_.constant_ring().allocate(sizeof(GxPsConstants), 256u);
        auto* pc = reinterpret_cast<GxPsConstants*>(ps.cpu);
        if (ps_override != nullptr) {
            std::memcpy(pc, ps_override, sizeof(GxPsConstants));
        } else {
            std::memset(pc, 0, sizeof(GxPsConstants));
        }
        pc->alpha_refs[3] = dst_alpha;
        UploadRing::Allocation palette = renderer_.matrix_ring().allocate(GxState::kMatrixPaletteWords * 4u, 256u);
        std::memset(palette.cpu, 0, GxState::kMatrixPaletteWords * 4u);
        auto* rows = reinterpret_cast<float*>(palette.cpu);
        for (int m = 0; m < 20; ++m) {  // identity position/texture matrices in the first rows
            for (int r = 0; r < 3; ++r) rows[(m * 3 + r) * 4 + r] = 1.0f;
        }
        UploadRing::Allocation vertices = renderer_.vertex_ring().allocate(6u * sizeof(GxVertexOut), 16u);
        auto* v = reinterpret_cast<GxVertexOut*>(vertices.cpu);
        std::memset(v, 0, 6u * sizeof(GxVertexOut));
        const auto cx = [](int x) { return static_cast<float>(x) / kW * 2.0f - 1.0f; };
        const auto cy = [](int y) { return 1.0f - static_cast<float>(y) / kH * 2.0f; };
        const float px[4] = {cx(x0), cx(x1), cx(x0), cx(x1)};
        const float py[4] = {cy(y0), cy(y0), cy(y1), cy(y1)};
        const int order[6] = {0, 1, 2, 2, 1, 3};
        for (int i = 0; i < 6; ++i) {
            const int c = order[i];
            v[i].position[0] = px[c];
            v[i].position[1] = py[c];
            v[i].position[2] = 0.5f;
            v[i].normal[2] = 1.0f;
            v[i].color0 = colours[static_cast<std::size_t>(c)];
            v[i].color1 = colours[static_cast<std::size_t>(c)];
            for (int t = 0; t < 8; ++t) {
                v[i].uv[t][0] = (px[c] + 1.0f) * 0.5f;
                v[i].uv[t][1] = (1.0f - py[c]) * 0.5f;
            }
        }
        DrawCall call{};
        call.pipeline = pipeline;
        call.vs_constants = vs.gpu;
        call.ps_constants = ps.gpu;
        call.matrix_palette = palette.gpu;
        call.vertex_view = gpu::vertex_view(vertices.gpu, 6u * sizeof(GxVertexOut), sizeof(GxVertexOut));
        call.index_count = 6u;
        call.topology = gpu::kTopologyTriangleList;
        call.scissor = gpu::rect(x0, y0, x1, y1);
        renderer_.bind_efb(nullptr);
        renderer_.draw(call);
    }

    // The pipeline for keys, in this mode: referencia blends dual-source pipelines with SRC1_ALPHA.
    gles::Pipeline* pipeline(const PsoKey& key, const VertexShaderKey& vs, const PixelShaderKey& ps) {
        gles::Pipeline* base = pipelines_.get(key, ps, vs);
        if (base == nullptr || mode_ != "referencia" || (key.render_state.blend_bits & (1u << 27u)) == 0u) {
            return base;
        }
        const auto found = reference_.find(key);
        if (found != reference_.end()) {
            return found->second.get();
        }
        auto made = std::make_unique<gles::Pipeline>(*base);
        made->program = reference_program(vs, ps);
        if (made->program == 0u) {
            ++missing_pipelines_;
            return nullptr;
        }
        const auto second = [](GLenum factor) {
            return factor == GL_SRC_ALPHA ? static_cast<GLenum>(GL_SRC1_ALPHA_EXT)
                : factor == GL_ONE_MINUS_SRC_ALPHA ? static_cast<GLenum>(GL_ONE_MINUS_SRC1_ALPHA_EXT) : factor;
        };
        made->blend_src_rgb = second(made->blend_src_rgb);
        made->blend_dst_rgb = second(made->blend_dst_rgb);
        gles::Pipeline* out = made.get();
        reference_.emplace(key, std::move(made));
        return out;
    }

    // Exact tiles: flat quads over the background, every pixel checked against the computed blend.
    void exact_scene(const std::string& name, const std::vector<Blend>& blends) {
        background();
        const Image before = read_efb();
        std::vector<Tile> tiles;
        static constexpr std::uint8_t kAlphas[] = {0, 2, 37, 64, 100, 127, 128, 170, 200, 254, 255};
        static constexpr std::array<std::array<std::uint8_t, 3>, 4> kColours = {{
            {{255, 255, 255}}, {{200, 40, 10}}, {{10, 180, 240}}, {{0, 0, 0}}}};
        const int columns = static_cast<int>(std::size(kAlphas));
        const int rows = static_cast<int>(blends.size() * kColours.size());
        const int tw = kW / columns, th = kH / std::max(rows, 1);
        int row = 0;
        for (const Blend& b : blends) {
            for (const auto& colour : kColours) {
                for (int c = 0; c < columns; ++c) {
                    Tile t{c * tw, row * th, c * tw + tw, row * th + th, colour[0], colour[1], colour[2], kAlphas[c],
                           static_cast<std::uint8_t>(255u - kAlphas[c] / 2u), b};
                    VertexShaderKey vs;
                    PixelShaderKey ps;
                    RenderStateKey rs;
                    passthrough_keys(b, vs, ps, rs);
                    const PsoKey key{vs.hash(), ps.hash(), rs};
                    const std::uint32_t rgba = (std::uint32_t{t.r} << 24u) | (std::uint32_t{t.g} << 16u) |
                        (std::uint32_t{t.b} << 8u) | t.a;
                    quad(pipeline(key, vs, ps), t.x0, t.y0, t.x1, t.y1, {rgba, rgba, rgba, rgba}, nullptr, nullptr,
                         t.k / 255.0f);
                    tiles.push_back(t);
                }
                ++row;
            }
        }
        const Image after = read_efb();
        save(name, before, after);
        // Every pixel of every tile against GX's blend over what the background left there.
        Check& check = checks_[name];
        for (const Tile& t : tiles) {
            for (int y = t.y0; y < t.y1; ++y) {
                for (int x = t.x0; x < t.x1; ++x) {
                    const std::size_t i = (static_cast<std::size_t>(y) * kW + x) * 4u;
                    const std::array<std::uint8_t, 4> want = gx_blend(t, &before.rgba[i], true);
                    const std::array<std::uint8_t, 4> old = gx_blend(t, &before.rgba[i], false);
                    int err = 0;
                    for (int ch = 0; ch < 4; ++ch) {
                        err = std::max(err, std::abs(static_cast<int>(after.rgba[i + ch]) - want[ch]));
                    }
                    ++check.pixels;
                    check.max_error = std::max(check.max_error, err);
                    check.over_1 += err > 1 ? 1u : 0u;
                    const bool differs = old != want;
                    check.affected += differs ? 1u : 0u;
                    if (t.blend.dual) ++check.dual_pixels; else ++check.control_pixels;
                    if (!t.blend.dual) {
                        check.control_max_error = std::max(check.control_max_error, err);
                    }
                }
            }
        }
    }

    // Alpha 0-255 across each tile, colour down it: the same blends, to look at.
    void gradient_scene(const std::string& name, const std::vector<Blend>& blends) {
        background();
        const Image before = read_efb();
        const int rows = static_cast<int>(blends.size());
        const int th = kH / rows;
        int row = 0;
        for (const Blend& b : blends) {
            VertexShaderKey vs;
            PixelShaderKey ps;
            RenderStateKey rs;
            passthrough_keys(b, vs, ps, rs);
            const PsoKey key{vs.hash(), ps.hash(), rs};
            quad(pipeline(key, vs, ps), 0, row * th, kW, row * th + th,
                 {0xFF402000u, 0xFF4020FFu, 0x20C0FF00u, 0x20C0FFFFu}, nullptr, nullptr, 0.75f);
            ++row;
        }
        save(name, before, read_efb());
    }

    // Four overlapping quads: dual-source over dual-source, with and without GXSetDstAlpha.
    void overlap_scene() {
        background();
        const Image before = read_efb();
        const Blend blends[4] = {
            {4, 5, true, false, false, 1, "srcalpha-invsrcalpha"}, {4, 1, true, false, false, 1, "srcalpha-one"},
            {4, 5, true, true, true, 1, "dstalpha"}, {5, 4, true, false, false, 1, "invsrcalpha-srcalpha"}};
        const std::array<std::uint32_t, 4> colours[4] = {
            {0xFF000080u, 0xFF0000F0u, 0xFF000010u, 0xFF000060u},
            {0x00FF0040u, 0x00FF00C0u, 0x00FF0090u, 0x00FF0020u},
            {0x2040FFA0u, 0x2040FF30u, 0x2040FFE0u, 0x2040FF70u},
            {0xFFFF20C8u, 0xFFFF2050u, 0xFFFF2010u, 0xFFFF20FFu}};
        const int boxes[4][4] = {{40, 40, 400, 330}, {200, 120, 600, 420}, {100, 220, 520, 500}, {300, 20, 620, 260}};
        for (int i = 0; i < 4; ++i) {
            VertexShaderKey vs;
            PixelShaderKey ps;
            RenderStateKey rs;
            passthrough_keys(blends[i], vs, ps, rs);
            const PsoKey key{vs.hash(), ps.hash(), rs};
            quad(pipeline(key, vs, ps), boxes[i][0], boxes[i][1], boxes[i][2], boxes[i][3], colours[i], nullptr,
                 nullptr, 0.4f);
        }
        save("superpuestas", before, read_efb());
    }

    // Real pipelines of the tour, one tile each: their shaders with made-up inputs (vertex colours, constants).
    void real_scene(const std::string& name, const std::vector<KeyRecord>& keys) {
        background();
        const Image before = read_efb();
        GxVsConstants vc{};
        for (int i = 0; i < 4; ++i) vc.projection[i][i] = 1.0f;
        for (int i = 0; i < 3; ++i) vc.inline_pos_matrix[i][i] = 1.0f;
        for (int t = 0; t < 8; ++t) {
            for (int i = 0; i < 2; ++i) vc.inline_tex_matrices[t][i][i] = 1.0f;
            for (int i = 0; i < 3; ++i) vc.inline_post_matrices[t][i][i] = 1.0f;
        }
        for (int c = 0; c < 2; ++c) {
            for (int i = 0; i < 4; ++i) {
                vc.chan_material[c][i] = 0.8f - 0.2f * i;
                vc.chan_ambient[c][i] = 0.3f + 0.15f * i;
            }
        }
        GxPsConstants pc{};
        for (int r = 0; r < 4; ++r) {
            for (int i = 0; i < 4; ++i) {
                pc.tev_regs[r][i] = 0.1f + 0.2f * static_cast<float>((r + i) % 5);
                pc.konst[r][i] = 0.9f - 0.2f * static_cast<float>((r * 3 + i) % 5);
            }
        }
        const int columns = 16, tw = kW / columns, th = kH / 16;
        for (std::size_t i = 0; i < keys.size() && i < 256u; ++i) {
            const KeyRecord& record = keys[i];
            const int x0 = static_cast<int>(i % columns) * tw, y0 = static_cast<int>(i / columns) * th;
            PsoKey key = record.key;
            key.render_state.primitive_topology = gpu::kTopologyTypeTriangle;
            key.render_state.zmode_bits = 0u;  // no depth test: every tile drawn
            key.render_state.cull = 0u;
            quad(pipeline(key, record.vs_key, record.ps_key), x0, y0, x0 + tw, y0 + th,
                 {0xF0A040FFu, 0x40F0A080u, 0xA040F040u, 0xFFFFFF10u}, &vc, &pc, 0.6f);
        }
        save(name, before, read_efb());
    }

    // The background: colour across, EFB alpha down (RGBA6, 6 bits), drawn without blending.
    void background() {
        renderer_.clear_efb(0u, 0u, 0xFFFFFFu, true, true, true);
        Blend opaque{1, 0, false, false, true, 1, "opaque"};
        VertexShaderKey vs;
        PixelShaderKey ps;
        RenderStateKey rs;
        passthrough_keys(opaque, vs, ps, rs);
        rs.blend_bits = (1u << 25u) | (1u << 26u);  // no blending, colour and alpha written
        const PsoKey key{vs.hash(), ps.hash(), rs};
        quad(pipelines_.get(key, ps, vs), 0, 0, kW, kH, {0xFF2000FFu, 0x20FF80FFu, 0x2080FF00u, 0xF0F0F000u});
    }

    void save(const std::string& name, const Image& before, const Image& after) {
        write_png(dir_ / mode_ / ("fondo-" + name + ".png"), before);
        write_png(dir_ / mode_ / (name + ".png"), after);
    }

    struct Check {
        std::uint64_t pixels = 0, dual_pixels = 0, control_pixels = 0, over_1 = 0, affected = 0;
        int max_error = 0, control_max_error = 0;
    };
    std::map<std::string, Check> checks_;
    std::filesystem::path dir_;
    unsigned missing_pipelines_ = 0;

private:
    GLuint reference_program(const VertexShaderKey& vs_key, const PixelShaderKey& ps_key) {
        const gles::ProgramHlsl hlsl = gles::program_hlsl(vs_key, ps_key, false);
        gles::TranslateOptions options;
        options.glsl_version = gles::Context::get().caps().glsl_version;
        options.palette_storage_block = renderer_.root_signature()->palette_storage_block;
        std::string error;
        const std::string vs = gles::shader_glsl(hlsl.vs, gles::Stage::Vertex, gles::ShaderKind::Gx, options, error);
        std::string ps = gles::shader_glsl(hlsl.ps, gles::Stage::Fragment, gles::ShaderKind::Gx, options, error);
        const std::string second = "layout(location = 1) out";
        const std::size_t at = ps.find(second);
        if (vs.empty() || at == std::string::npos) {
            std::printf("FAIL referencia: %s\n", error.c_str());
            return 0u;
        }
        ps.replace(at, second.size(), "layout(location = 0, index = 1) out");
        ps.insert(ps.find('\n') + 1u, "#extension GL_EXT_blend_func_extended : require\n");
        const auto compile = [](GLenum stage, const std::string& text) -> GLuint {
            const GLuint shader = glCreateShader(stage);
            const char* source = text.c_str();
            glShaderSource(shader, 1, &source, nullptr);
            glCompileShader(shader);
            GLint ok = GL_FALSE;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
            if (ok != GL_TRUE) {
                char log[2048]{};
                glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
                std::printf("FAIL referencia compile: %s\n", log);
                return 0u;
            }
            return shader;
        };
        const GLuint v = compile(GL_VERTEX_SHADER, vs);
        const GLuint p = compile(GL_FRAGMENT_SHADER, ps);
        if (v == 0u || p == 0u) {
            return 0u;
        }
        const GLuint program = glCreateProgram();
        glAttachShader(program, v);
        glAttachShader(program, p);
        glLinkProgram(program);
        GLint ok = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &ok);
        if (ok != GL_TRUE) {
            std::printf("FAIL referencia link\n");
            return 0u;
        }
        gles::apply_bindings(program, gles::bindings_of(vs, ps));
        return program;
    }

    RendererGles& renderer_;
    PipelineCacheGles& pipelines_;
    std::string mode_;
    std::unordered_map<PsoKey, std::unique_ptr<gles::Pipeline>, PsoKeyHasher> reference_;
};

// The blends Galaxy's dual-source pipelines use (vk_seed_keys.bin: 172, 62, 5, 1 and 1 pipelines), each with the EFB
// alpha write off (202 of 241), on (3) and with GXSetDstAlpha (35); then controls that are not dual source.
std::vector<Blend> galaxy_blends() {
    return {
        {4, 5, true, false, false, 1, "srcalpha-invsrcalpha"},
        {4, 5, true, false, true, 1, "srcalpha-invsrcalpha+alfa"},
        {4, 5, true, true, true, 1, "srcalpha-invsrcalpha+dstalpha"},
        {4, 1, true, false, false, 1, "srcalpha-one"},
        {4, 1, true, true, true, 1, "srcalpha-one+dstalpha"},
        {4, 2, true, false, false, 1, "srcalpha-srcclr"},
        {5, 4, true, false, true, 1, "invsrcalpha-srcalpha+alfa"},
        {4, 0, true, false, false, 1, "srcalpha-zero"},
    };
}

std::vector<Blend> control_blends() {
    return {
        {4, 5, false, false, false, 0, "rgb8-srcalpha-invsrcalpha"},
        {1, 1, false, false, true, 1, "rgba6-one-one"},
        {6, 7, false, false, true, 1, "rgba6-dstalpha-invdstalpha"},
        {2, 0, false, false, false, 1, "rgba6-dstclr-zero"},
    };
}

// ---- Comparison of the modes' images.

struct Metrics {
    std::uint64_t pixels = 0, different = 0;
    double mean[4]{};
    int max[4]{};
};

Metrics compare(const Image& a, const Image& b, Image* diff) {
    Metrics m;
    const std::size_t n = std::min(a.rgba.size(), b.rgba.size()) / 4u;
    double sum[4]{};
    if (diff != nullptr) {
        diff->rgba.assign(a.rgba.size(), 0u);
    }
    for (std::size_t i = 0; i < n; ++i) {
        int worst = 0;
        for (int c = 0; c < 4; ++c) {
            const int e = std::abs(static_cast<int>(a.rgba[i * 4 + c]) - b.rgba[i * 4 + c]);
            sum[c] += e;
            m.max[c] = std::max(m.max[c], e);
            worst = std::max(worst, e);
        }
        m.different += worst != 0 ? 1u : 0u;
        if (diff != nullptr) {
            // Error at 32 steps per level (1 LSB visible), red where colour differs, blue alpha; opaque.
            int ec = 0;
            for (int c = 0; c < 3; ++c) ec = std::max(ec, std::abs(static_cast<int>(a.rgba[i * 4 + c]) - b.rgba[i * 4 + c]));
            const int ea = std::abs(static_cast<int>(a.rgba[i * 4 + 3]) - b.rgba[i * 4 + 3]);
            diff->rgba[i * 4 + 0] = static_cast<std::uint8_t>(std::min(255, ec * 32));
            diff->rgba[i * 4 + 1] = static_cast<std::uint8_t>(worst == 0 ? 24 : 0);
            diff->rgba[i * 4 + 2] = static_cast<std::uint8_t>(std::min(255, ea * 32));
            diff->rgba[i * 4 + 3] = 255u;
        }
    }
    m.pixels = n;
    for (int c = 0; c < 4; ++c) m.mean[c] = n != 0u ? sum[c] / static_cast<double>(n) : 0.0;
    return m;
}

int compare_all(const std::filesystem::path& dir) {
    static const char* kScenes[] = {"exactas-galaxy", "exactas-controles", "gradientes", "superpuestas", "claves-reales",
                                    "controles-reales"};
    std::ofstream report(dir / "metricas.txt", std::ios::trunc);
    report << "escena | comparacion | pixeles | % distintos | error medio R G B A | error max R G B A\n";
    int failures = 0;
    for (const char* scene : kScenes) {
        Image images[3];
        const char* modes[3] = {"referencia", "plain", "fetch"};
        bool have[3]{};
        for (int i = 0; i < 3; ++i) {
            have[i] = read_png(dir / modes[i] / (std::string(scene) + ".png"), images[i]);
        }
        const auto line = [&](int a, int b) {
            if (!have[a] || !have[b]) {
                report << scene << " | " << modes[a] << " vs " << modes[b] << " | (falta una imagen)\n";
                return Metrics{};
            }
            Image diff;
            const Metrics m = compare(images[a], images[b], &diff);
            write_png(dir / "dif" / (std::string(scene) + "-" + modes[a] + "-vs-" + modes[b] + ".png"), diff);
            char text[256];
            std::snprintf(text, sizeof(text), "%s | %s vs %s | %llu | %.3f | %.4f %.4f %.4f %.4f | %d %d %d %d\n",
                          scene, modes[a], modes[b], static_cast<unsigned long long>(m.pixels),
                          100.0 * static_cast<double>(m.different) / static_cast<double>(std::max<std::uint64_t>(m.pixels, 1u)),
                          m.mean[0], m.mean[1], m.mean[2], m.mean[3], m.max[0], m.max[1], m.max[2], m.max[3]);
            report << text;
            std::fputs(text, stdout);
            return m;
        };
        const Metrics ref_plain = line(0, 1);
        const Metrics ref_fetch = line(0, 2);
        const Metrics plain_fetch = line(1, 2);
        (void)ref_plain;
        const bool controls = std::string(scene).find("control") != std::string::npos;
        if (have[0] && have[2]) {
            const int worst = std::max({ref_fetch.max[0], ref_fetch.max[1], ref_fetch.max[2], ref_fetch.max[3]});
            if (worst > 1) {
                std::printf("FAIL %s: fetch differs from the reference by %d (tolerance 1)\n", scene, worst);
                ++failures;
            }
        }
        if (controls && have[1] && have[2] && plain_fetch.different != 0u) {
            std::printf("FAIL %s: pipelines that are not dual source changed with fetch\n", scene);
            ++failures;
        }
    }
    std::printf("blend comparison: %d failed\n", failures);
    return failures == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc >= 3 && std::string(argv[1]) == "--comparar") {
        return compare_all(argv[2]);
    }
    if (argc < 4 || std::string(argv[1]) != "--modo") {
        std::printf("uso: nebula_gles_blend_tests --modo <plain|fetch|referencia> <carpeta> | --comparar <carpeta>\n");
        return 2;
    }
    const std::string mode = argv[2];
    if (mode != "plain" && mode != "fetch" && mode != "referencia") {
        return 2;
    }
    // plain and referencia: the dual-source pipelines on fixed-function blending (referencia then swaps in its own).
    setenv("GALAXY_GLES_BLEND_FETCH", mode == "fetch" ? "1" : "0", 1);
    setenv("NEBULA_WINDOWED", "1", 0);
    setenv("GALAXY_GLES_NO_BINARIES", "1", 1);
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
    const std::filesystem::path out_dir = std::filesystem::path(argv[3]) / mode;
    std::filesystem::create_directories(out_dir);
    const std::string cache = (std::filesystem::path(argv[3]) / ("cache-" + mode)).string();
    if (!pipelines.initialize(renderer.device(), renderer.root_signature(), cache, 1u)) {
        std::printf("FAIL pipeline cache initialize\n");
        return 2;
    }
    const gles::FramebufferFetch fetch = pipelines.fetch_blend();
    std::printf("modo %s: framebuffer fetch %s, GL_EXT_blend_func_extended %s\n", mode.c_str(),
                fetch == gles::FramebufferFetch::Arm ? "ARM" : fetch == gles::FramebufferFetch::Ext ? "EXT" : "no",
                gles::gl::has_extension("GL_EXT_blend_func_extended") ? "si" : "no");
    if (mode == "fetch" && fetch == gles::FramebufferFetch::None) {
        std::printf("FAIL the driver has no framebuffer fetch\n");
        return 3;
    }
    if (mode == "referencia" && !gles::gl::has_extension("GL_EXT_blend_func_extended")) {
        std::printf("FAIL the driver has no GL_EXT_blend_func_extended: no reference here\n");
        return 3;
    }
    renderer.begin_frame(true);  // the context is current on this thread from here
    {
        // The driver as this run saw it (the console's checks: docs/GX_ES_STEP2_RESULTS.md).
        std::ofstream info(out_dir / "gl.txt", std::ios::trunc);
        const auto text = [](GLenum name) {
            const GLubyte* value = glGetString(name);
            return value != nullptr ? std::string(reinterpret_cast<const char*>(value)) : std::string();
        };
        const gles::Capabilities& caps = gles::Context::get().caps();
        info << "GL_VERSION " << text(GL_VERSION) << "\nGL_RENDERER " << text(GL_RENDERER) << "\nGL_VENDOR "
             << text(GL_VENDOR) << "\nGL_SHADING_LANGUAGE_VERSION " << text(GL_SHADING_LANGUAGE_VERSION)
             << "\ncontexto OpenGL ES " << caps.version_major << '.' << caps.version_minor
             << (caps.forced_es30 ? " (GALAXY_GLES_FORCE_ES30)" : "") << ", GLSL ES " << caps.glsl_version
             << ", paleta " << (caps.vertex_storage_blocks ? "SSBO" : caps.texture_buffer ? "texture buffer" : "2D")
             << ", buffers " << (caps.buffer_storage ? "persistentes" : "copiados") << "\nframebuffer fetch "
             << (fetch == gles::FramebufferFetch::Arm ? "ARM" : fetch == gles::FramebufferFetch::Ext ? "EXT" : "no")
             << " (GL_ARM_shader_framebuffer_fetch " << (caps.framebuffer_fetch_arm ? "si" : "no")
             << ", GL_EXT_shader_framebuffer_fetch " << (caps.framebuffer_fetch_ext ? "si" : "no") << ")\n";
        GLint count = 0;
        glGetIntegerv(GL_NUM_EXTENSIONS, &count);
        info << "GL_EXTENSIONS (" << count << ")\n";
        for (GLint i = 0; i < count; ++i) {
            if (const GLubyte* name = glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i))) {
                info << "  " << reinterpret_cast<const char*>(name) << '\n';
            }
        }
    }
    Scenes scenes(renderer, pipelines, mode);
    scenes.dir_ = argv[3];
    scenes.exact_scene("exactas-galaxy", galaxy_blends());
    scenes.exact_scene("exactas-controles", control_blends());
    {
        std::vector<Blend> both = galaxy_blends();
        for (const Blend& b : control_blends()) both.push_back(b);
        scenes.gradient_scene("gradientes", both);
    }
    scenes.overlap_scene();
    renderer.end_frame(true);
    renderer.begin_frame(true);
    if (const char* keys_path = std::getenv("NEBULA_SEMILLA")) {
        const std::vector<KeyRecord> all = read_keys(keys_path);
        std::vector<KeyRecord> dual, plain;
        for (const KeyRecord& record : all) {
            const bool is_dual = (record.key.render_state.blend_bits & (1u << 27u)) != 0u;
            if (is_dual) {
                dual.push_back(record);
            } else if ((record.key.render_state.blend_bits & 1u) != 0u || plain.size() < 120u) {
                plain.push_back(record);
            }
        }
        std::printf("claves: %zu, de ellas %zu con doble fuente\n", all.size(), dual.size());
        scenes.real_scene("claves-reales", dual);
        renderer.end_frame(true);
        renderer.begin_frame(true);
        scenes.real_scene("controles-reales", plain);
    } else {
        std::printf("(sin NEBULA_SEMILLA: sin las escenas de claves reales)\n");
    }
    renderer.end_frame(true);

    // The exact scenes against GX's blend computed here.
    int failures = 0;
    for (const auto& [name, check] : scenes.checks_) {
        std::printf("%s: %llu pixeles (%llu de doble fuente, %llu de control); %llu con error > 1; error max %d (controles "
                    "%d); %llu pixeles donde la mezcla sin TEV alfa da otro valor\n",
                    name.c_str(), static_cast<unsigned long long>(check.pixels),
                    static_cast<unsigned long long>(check.dual_pixels),
                    static_cast<unsigned long long>(check.control_pixels),
                    static_cast<unsigned long long>(check.over_1), check.max_error, check.control_max_error,
                    static_cast<unsigned long long>(check.affected));
        {
            std::ofstream info(out_dir / "gl.txt", std::ios::app);
            info << name << ": " << check.pixels << " pixeles, " << check.over_1 << " con error > 1, error max "
                 << check.max_error << " (controles " << check.control_max_error << ")\n";
        }
        // fetch and referencia must give GX's blend (tolerance 1); plain must give it on the controls.
        const bool ok = mode == "plain" ? check.control_max_error <= 1 : check.max_error <= 1;
        if (!ok) {
            std::printf("FAIL %s en modo %s\n", name.c_str(), mode.c_str());
            ++failures;
        }
    }
    if (scenes.missing_pipelines_ != 0u) {
        std::printf("FAIL %u draws sin pipeline\n", scenes.missing_pipelines_);
        ++failures;
    }
    pipelines.shutdown();
    renderer.shutdown();
    {
        // Peak memory of this run (VmHWM) and what the system has free now.
        std::ofstream info(out_dir / "gl.txt", std::ios::app);
        for (const char* file : {"/proc/self/status", "/proc/meminfo"}) {
            std::ifstream in(file);
            std::string line;
            while (std::getline(in, line)) {
                if (line.rfind("VmHWM", 0) == 0 || line.rfind("MemAvailable", 0) == 0) {
                    info << line << '\n';
                }
            }
        }
        info << "fallos " << failures << '\n';
    }
    std::printf("gles blend tests (%s): %d failed\n", mode.c_str(), failures);
    return failures == 0 ? 0 : 1;
}
