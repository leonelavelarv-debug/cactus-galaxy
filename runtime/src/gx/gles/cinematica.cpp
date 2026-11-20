// SPDX-License-Identifier: GPL-3.0-only
// Cutscene pacing (cinematica.h): GALAXY_CINEMATICA_RITMO=1 presents a cutscene's new pictures every k vblanks.
#include "galaxy/gx/cinematica.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

namespace galaxy::gx::cinematica {
namespace {

std::int64_t now_ns() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// The swapping thread's own state (one thread swaps).
std::int64_t last_paced_end_ns = 0;
int vblanks = 0;  // k in force (0: none yet)
int up_votes = 0;
int down_votes = 0;
std::uint64_t paced_count = 0;

}  // namespace

bool ritmo_habilitado() noexcept {
    static const bool enabled = [] {
        const char* value = std::getenv("GALAXY_CINEMATICA_RITMO");
        const bool on = value != nullptr && value[0] == '1';
        if (on) {
            std::fprintf(stderr, "[cinematica] GALAXY_CINEMATICA_RITMO=1: en las cinematicas cada imagen nueva cada k "
                                 "vblanks, sin repeticiones ni bajar el salto\n");
        }
        return on;
    }();
    return enabled;
}

int video_modo() noexcept {
    static const int mode = [] {
        const char* value = std::getenv("GALAXY_VIDEO_SIN_SALTO");
        const int m = value != nullptr ? std::atoi(value) : 0;
        if (m >= 1 && m <= 3) {
            std::fprintf(stderr, "[cinematica] GALAXY_VIDEO_SIN_SALTO=%d: en los videos %s\n", m,
                         m == 1 ? "sin salto de cuadros" : m == 2 ? "sin salto de cuadros y con ritmo parejo"
                                                         : "con salto, sin presentar las repeticiones");
            return m;
        }
        return 0;
    }();
    return mode;
}

bool video_activo() noexcept {
    const std::int64_t last = ultimo_video_ns.load(std::memory_order_relaxed);
    return last != 0 && now_ns() - last < 250000000LL;
}

void video_cuadro() noexcept {
    const bool was = video_activo();
    ultimo_video_ns.store(now_ns(), std::memory_order_relaxed);
    if (!was && video_modo() != 0) {
        std::fprintf(stderr, "[cinematica] video: empieza\n");
    }
}

void esperar_turno(std::int64_t period_ns) noexcept {
    if (period_ns <= 0) {
        period_ns = 1000000000LL / 60;
    }
    const std::int64_t now = now_ns();
    if (last_paced_end_ns == 0 || now - last_paced_end_ns > 500000000LL) {
        // The first paced picture, or after a pause (a load, a menu): no wait, the count starts here.
        up_votes = 0;
        down_votes = 0;
        return;
    }
    // How many vblanks this picture takes as it comes (ready at `gap`, shown at the next vblank).
    const std::int64_t gap = now - last_paced_end_ns;
    const int wanted = static_cast<int>(std::clamp<std::int64_t>((gap + period_ns - 1) / period_ns, 1, 6));
    if (vblanks == 0) {
        vblanks = wanted;
    } else if (wanted > vblanks) {
        down_votes = 0;
        // Late twice in a row: one vblank more (a single late picture is shown late, as it comes).
        if (++up_votes >= 2) {
            vblanks = std::min(vblanks + 1, 6);
            up_votes = 0;
        }
    } else if (wanted < vblanks) {
        up_votes = 0;
        // Early for about a second's worth of pictures: one vblank less.
        if (++down_votes >= 20) {
            vblanks = std::max(vblanks - 1, 1);
            down_votes = 0;
        }
    } else {
        up_votes = 0;
        down_votes = 0;
    }
    // Wake a third of a period before the k-th vblank: the swap itself waits for it.
    const std::int64_t target = last_paced_end_ns + vblanks * period_ns - period_ns / 3;
    if (now < target) {
        std::this_thread::sleep_for(std::chrono::nanoseconds(target - now));
    }
    if ((++paced_count % 600u) == 1u) {
        std::fprintf(stderr, "[cinematica] ritmo: cada %d vblanks (%.1f imagenes/s)\n", vblanks,
                     1e9 / static_cast<double>(vblanks * period_ns));
    }
}

void swap_hecho(bool paced) noexcept {
    last_paced_end_ns = paced ? now_ns() : 0;
    if (!paced) {
        vblanks = 0;
    }
}

}  // namespace galaxy::gx::cinematica
