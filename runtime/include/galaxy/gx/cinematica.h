// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <atomic>
#include <cstdint>

namespace galaxy::gx::cinematica {

// 1 while the game runs a cutscene (NEBULA_CONTEXT_DEMO, MR::isDemoActive), set on the simulation thread by the host
// (host_main.cpp's svc_game_context) and back to 0 when the stage changes.
inline std::atomic<int> activa{0};

// GALAXY_CINEMATICA_RITMO=1 (off by default): during a cutscene the frame skip does not go down, its repeats are not
// presented and each new picture is presented every k vblanks (gx_backend.cpp, renderer_gles.cpp).
bool ritmo_habilitado() noexcept;

// steady_clock time (ns) of the last movie frame the game decoded (host_main.cpp's svc_thp_video_decode).
inline std::atomic<std::int64_t> ultimo_video_ns{0};
// GALAXY_VIDEO_SIN_SALTO (off by default): 1 = while a movie plays (a frame decoded in the last 250 ms) the frame skip
// is off, so every decoded movie frame is drawn; 2 = that and its pictures paced like a cutscene's (esperar_turno); 3 = the skip as usual, its repeats not
// presented (no swap for them: the game is not held by the vsync of pictures that do not change).
int video_modo() noexcept;
bool video_activo() noexcept;
// Records a decoded movie frame (the host calls it).
void video_cuadro() noexcept;

// Set by the render thread for the frame it presents: this present is a cutscene's paced new picture.
inline std::atomic<bool> presentar_con_ritmo{false};

// On the thread that swaps, before the swap of a paced picture: waits so that it lands k vblanks after the previous
// paced one (k from the measured gap, 1-6, with hysteresis). `period_ns` is the display's refresh period.
void esperar_turno(std::int64_t period_ns) noexcept;
// After any swap: when it ended (the next paced one counts from here).
void swap_hecho(bool paced) noexcept;

}  // namespace galaxy::gx::cinematica
