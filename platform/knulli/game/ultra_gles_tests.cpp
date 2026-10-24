// SPDX-License-Identifier: GPL-3.0-only
// ULTRA-GLES's profiler on made-up frames: scenes, load windows, hitches and their causes, GPU times that arrive late,
// captures and the reports (resumen.json is checked by run with python3 in the build's test step).
//   GALAXY_ULTRA=bench GALAXY_ULTRA_DIR=<dir> nebula_ultra_gles_tests

#include "galaxy/gx/gles/ultra_gles.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

namespace ultra = galaxy::gx::ultra;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

}  // namespace

int main() {
    check(ultra::enabled(), "GALAXY_ULTRA turns it on");
    std::int64_t t = 1'000'000'000;
    std::uint64_t index = 0;
    std::string captured;
    double gpu_of[8] = {};
    const auto present = [&](double interval_ms, double gpu_ms, std::uint32_t draws, bool repeat) {
        t += static_cast<std::int64_t>(interval_ms * 1e6);
        ultra::FrameSample s{};
        s.frame_index = index;
        s.present_ns = t;
        s.cpu_ns = 6'000'000;
        s.draw_ns = 3'000'000;
        s.draws = draws;
        s.program_changes = draws / 4u;
        s.repeats = repeat ? 1u : 0u;
        s.efb_width = 320u;
        s.efb_height = 264u;
        ultra::frame(s);
        gpu_of[index % 8u] = gpu_ms;
        if (index >= 3u) {
            ultra::gpu(index - 3u, gpu_of[(index - 3u) % 8u]);  // three frames late, as the timer queries come back
        }
        if (std::string path = ultra::capture_path(); !path.empty()) {
            captured = path;
        }
        ++index;
    };
    // Scene A: 3 s of load, then 20 s at 60 with a shader hitch every 2 s.
    ultra::set_scene("TestAGalaxy");
    for (int i = 0; i < 180; ++i) {
        present(16.667, 9.0, 200u, false);
    }
    for (int i = 0; i < 1200; ++i) {
        if (i % 120 == 60) {
            ultra::add(ultra::kSyncCompiles, 1u);
            ultra::add(ultra::kSyncCompileNs, 25'000'000u);
            present(41.0, 9.0, 200u, false);
        } else {
            present(16.667, 9.0, 200u, false);
        }
    }
    check(captured.find("capturas/TestAGalaxy.ppm") != std::string::npos, "scene A's capture path");
    // Scene B: GPU-bound, every other frame repeated (30 drawn), a long GPU frame now and then.
    ultra::set_scene("TestBGalaxy");
    for (int i = 0; i < 180 + 900; ++i) {
        const bool slow = i > 180 && i % 90 == 0;
        present(slow ? 33.4 : 16.667, slow ? 30.0 : 14.0, 400u, i % 2 == 1);
    }
    for (int i = 0; i < 20; ++i) {
        ultra::add(ultra::kTextureUploads, 1u);
        ultra::add(ultra::kTextureUploadBytes, 64u * 1024u);
        present(16.667, 14.0, 400u, false);
    }
    ultra::finish();
    ultra::finish();  // idempotent
    check(!captured.empty() && captured.find("TestBGalaxy") != std::string::npos, "scene B's capture path");
    std::printf("ultra gles tests: %d failed\n", failures);
    return failures == 0 ? 0 : 1;
}
