// SPDX-License-Identifier: GPL-3.0-only
// nebula_glsl_aot: the GLSL library (shader_library_gles.h) on the PC, so the console's Nebula hands GLSL straight to
// the driver and carries no translator.
//
//   nebula_glsl_aot <library.bin> [--formas 320t,320s,310t,310s] <keys>...
//       every GX program the keys use (gles_keys.bin of a console session, "GLEK", or the Vulkan seed of a whole tour,
//       platform/android/vk_seed_keys.bin, "GXVK": the same records), the uber shaders and the renderer's own, each
//       translated (shader_translate_gles.h) in every form asked: GLSL ES 320 or 310, the matrix palette as a texture
//       buffer (t: Mali's driver) or a storage block (s). Its HLSL is made exactly as the GLES pipeline cache and
//       renderer make it (shader_sources_gles.h), so the entries match.
//   nebula_glsl_aot --verificar <library.bin> [--formas ...] <keys>...
//       translates again and compares: every entry the keys need is there and is that GLSL.
// No GL context: only the translator runs.
#include "galaxy/gx/gles/shader_sources_gles.h"
#include "galaxy/gx/gles/shader_library_gles.h"
#include "galaxy/gx/gles/shader_translate_gles.h"
#include "galaxy/gx/gpu_types.h"
#include "galaxy/gx/uber_shaders.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

using namespace galaxy::gx;

namespace {

struct KeyRecord {
    PsoKey key{};
    VertexShaderKey vs_key{};
    PixelShaderKey ps_key{};
};

struct Form {
    int glsl = 320;
    bool storage = false;
};

struct Job {
    std::string hlsl;
    gles::Stage stage = gles::Stage::Vertex;
    gles::ShaderKind kind = gles::ShaderKind::Gx;
    gles::TranslateOptions options;
    std::uint64_t key = 0;
    std::string what;
};

bool read_keys(const char* path, std::vector<KeyRecord>& out) {
    std::ifstream in(path, std::ios::binary);
    std::uint32_t magic = 0, version = 0, count = 0;
    if (!in.read(reinterpret_cast<char*>(&magic), 4) || !in.read(reinterpret_cast<char*>(&version), 4) ||
        !in.read(reinterpret_cast<char*>(&count), 4) || (magic != 0x4b454c47u && magic != 0x4b565847u) ||
        version != 1u || count > 65536u) {
        std::fprintf(stderr, "%s: not a key file (GLEK or GXVK, version 1)\n", path);
        return false;
    }
    std::size_t kept = 0, bad = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        KeyRecord r;
        if (!in.read(reinterpret_cast<char*>(&r), sizeof(r))) {
            break;
        }
        // The same check the cache makes when it reads its own file: a record of another layout fails it.
        if (r.key.vs_hash != r.vs_key.hash() || r.key.ps_hash != r.ps_key.hash()) {
            ++bad;
            continue;
        }
        out.push_back(r);
        ++kept;
    }
    std::fprintf(stderr, "%s: %zu keys%s\n", path, kept, bad != 0u ? " (some of another layout skipped)" : "");
    return kept != 0u || count == 0u;
}

bool parse_forms(const char* text, std::vector<Form>& out) {
    out.clear();
    std::string s(text);
    std::size_t at = 0;
    while (at <= s.size()) {
        const std::size_t end = std::min(s.find(',', at), s.size());
        const std::string f = s.substr(at, end - at);
        if (f == "320t") out.push_back({320, false});
        else if (f == "320s") out.push_back({320, true});
        else if (f == "310t") out.push_back({310, false});
        else if (f == "310s") out.push_back({310, true});
        else return false;
        at = end + 1;
    }
    return !out.empty();
}

}  // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    int a = 1;
    const bool verify = a < argc && std::strcmp(argv[a], "--verificar") == 0;
    if (verify) ++a;
    if (a >= argc) {
        std::fprintf(stderr, "uso: nebula_glsl_aot [--verificar] <biblioteca.bin> [--formas 320t,320s,310t,310s] "
                             "<claves>...\n");
        return 2;
    }
    const std::string library_path = argv[a++];
    std::vector<Form> forms{{320, false}, {320, true}, {310, false}, {310, true}};
    if (a + 1 < argc && std::strcmp(argv[a], "--formas") == 0) {
        if (!parse_forms(argv[a + 1], forms)) {
            std::fprintf(stderr, "--formas: 320t, 320s, 310t, 310s separadas por comas\n");
            return 2;
        }
        a += 2;
    }
    std::vector<KeyRecord> keys;
    for (; a < argc; ++a) {
        if (!read_keys(argv[a], keys)) return 2;
    }
    const auto start = std::chrono::steady_clock::now();

    // The HLSL of every program, once: GX pairs (by their shader keys and point form), then the fixed ones.
    std::vector<std::pair<std::string, std::string>> gx;  // vs, ps
    {
        std::set<std::tuple<std::uint64_t, std::uint64_t, bool>> seen;
        for (const KeyRecord& r : keys) {
            const bool points = r.key.render_state.primitive_topology == gpu::kTopologyTypePointValue;
            if (!seen.insert({r.vs_key.hash(), r.ps_key.hash(), points}).second) continue;
            const gles::ProgramHlsl source = gles::program_hlsl(r.vs_key, r.ps_key, points);
            gx.emplace_back(source.vs, source.ps);
        }
    }
    std::vector<Job> jobs;
    std::set<std::uint64_t> queued;
    const auto add = [&](const std::string& hlsl, gles::Stage stage, gles::ShaderKind kind, const Form& form,
                         std::string what) {
        Job j;
        j.hlsl = hlsl;
        j.stage = stage;
        j.kind = kind;
        j.options.glsl_version = form.glsl;
        j.options.palette_storage_block = form.storage;
        j.key = gles::library_key(hlsl, stage, kind, j.options);
        j.what = std::move(what);
        if (queued.insert(j.key).second) jobs.push_back(std::move(j));
    };
    for (const Form& form : forms) {
        const std::string f = std::to_string(form.glsl) + (form.storage ? "s" : "t");
        // The renderer's own (no palette: one entry per GLSL version).
        add(std::string(gles::utility_vertex_hlsl()), gles::Stage::Vertex, gles::ShaderKind::Utility, form,
            "utility vertex " + f);
        add(std::string(gles::conversion_hlsl()), gles::Stage::Fragment, gles::ShaderKind::Utility, form,
            "EFB conversion " + f);
        add(gles::display_copy_hlsl(), gles::Stage::Fragment, gles::ShaderKind::Utility, form,
            "display copy " + f);
        add(gles::uber_vertex_hlsl(), gles::Stage::Vertex, gles::ShaderKind::Gx, form, "uber vertex " + f);
        for (unsigned v = 0; v < kUberPsVariants; ++v) {
            add(gles::uber_pixel_hlsl(v), gles::Stage::Fragment, gles::ShaderKind::Gx, form,
                "uber pixel " + std::to_string(v) + " " + f);
        }
        // Vertex shaders together, then pixel shaders: neighbours share most of their text (smaller blocks).
        for (std::size_t i = 0; i < gx.size(); ++i) {
            add(gx[i].first, gles::Stage::Vertex, gles::ShaderKind::Gx, form, "vertex " + std::to_string(i) + " " + f);
        }
        for (std::size_t i = 0; i < gx.size(); ++i) {
            add(gx[i].second, gles::Stage::Fragment, gles::ShaderKind::Gx, form, "pixel " + std::to_string(i) + " " + f);
        }
    }

    // Translated on every core.
    std::vector<std::string> glsl(jobs.size());
    std::atomic<std::size_t> next{0};
    std::atomic<int> failures{0};
    std::mutex report;
    std::vector<std::thread> threads;
    const unsigned workers = std::max(1u, std::thread::hardware_concurrency());
    for (unsigned t = 0; t < workers; ++t) {
        threads.emplace_back([&] {
            for (std::size_t i; (i = next.fetch_add(1u)) < jobs.size();) {
                std::string error;
                glsl[i] = gles::translate(jobs[i].hlsl, jobs[i].stage, jobs[i].kind, jobs[i].options, error);
                if (glsl[i].empty()) {
                    std::lock_guard<std::mutex> lock(report);
                    std::fprintf(stderr, "no se tradujo %s: %s\n", jobs[i].what.c_str(), error.c_str());
                    ++failures;
                }
            }
        });
    }
    for (std::thread& t : threads) t.join();
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();

    if (verify) {
        std::vector<std::pair<std::uint64_t, std::string>> entries;
        std::string error;
        if (!gles::read_shader_library(library_path, entries, error)) {
            std::fprintf(stderr, "%s\n", error.c_str());
            return 1;
        }
        std::map<std::uint64_t, std::string> by_key(entries.begin(), entries.end());
        std::size_t missing = 0, different = 0;
        for (std::size_t i = 0; i < jobs.size(); ++i) {
            const auto it = by_key.find(jobs[i].key);
            if (it == by_key.end()) ++missing;
            else if (it->second != glsl[i]) ++different;
        }
        std::printf("verificar: %zu entradas en la biblioteca, %zu necesarias, %zu faltan, %zu distintas\n",
                    entries.size(), jobs.size(), missing, different);
        return missing == 0u && different == 0u && failures == 0 ? 0 : 1;
    }

    std::vector<std::pair<std::uint64_t, std::string>> entries;
    std::size_t raw = 0;
    for (std::size_t i = 0; i < jobs.size(); ++i) {
        if (glsl[i].empty()) continue;
        raw += glsl[i].size();
        entries.emplace_back(jobs[i].key, std::move(glsl[i]));
    }
    std::string error;
    if (!gles::write_shader_library(library_path, std::move(entries), error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    std::ifstream out(library_path, std::ios::binary | std::ios::ate);
    std::printf("biblioteca GLSL: %zu programas GX, %zu shaders en %zu formas (%.1f MiB de GLSL, %.1f MiB en disco), "
                "%d sin traducir, %.1f s\n",
                gx.size(), jobs.size() - static_cast<std::size_t>(failures.load()), forms.size(), raw / 1048576.0,
                static_cast<double>(out.tellg()) / 1048576.0, failures.load(), ms / 1000.0);
    return failures == 0 ? 0 : 1;
}
