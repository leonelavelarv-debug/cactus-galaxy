// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Nopal's debug channel for the GLES port: with NOPAL_DEPURAR=<port> Nebula listens on TCP (one client: the Depurador
// of Nopal Estudio, on the PC, by WiFi or in the simulator) and speaks the protocol of Nopal's runtime (nopal/sdk,
// depurar.cpp), so the Estudio shows Galaxy's frames, memory and log as it does a Nopal game's. One line a message:
//   Nebula -> Estudio:  HOLA nopal 1 <consola|sim> <title>
//                       F <frame> <frame_ms> <cpu_ms> <steps> draw=<ms> gpu=<ms> vsync=<ms> gl=<ms> ...  (each present)
//                       R <ram_mib> <cpu_%>                                                        (every 30 presents)
//                       L <a line of the log>       (stderr, which still goes where it went: log.txt)
//                       ESTADO <pausa|sigue>
//   Estudio -> Nebula:  pausa | sigue | paso | overlay | salir
// pausa holds the render thread after its present (the game waits for it as for a slow GPU) until sigue, or for one
// frame with paso; salir asks the process to end (SIGTERM, as the launcher's exit combination does). Without
// NOPAL_DEPURAR nothing is opened and each call below is one predictable branch. The game never waits for the
// Estudio: lines that do not fit in the send queue are dropped.
#include "galaxy/gx/gles/ultra_gles.h"

namespace galaxy::gx::nopal_depurar {

// Opens the channel on the first call when NOPAL_DEPURAR names a port; true while it is open.
[[nodiscard]] bool active() noexcept;

// The render thread, after each present: the frame's line (and every 30th, memory and CPU), then a pause if asked.
void frame(const ultra::FrameSample& sample) noexcept;

// The stage the game reports (host_main.cpp's svc_game_stage), as a log line.
void scene(const char* name) noexcept;

// Tests: the port the channel listens on (0 when closed), and closing it.
[[nodiscard]] int port() noexcept;
void close() noexcept;

}  // namespace galaxy::gx::nopal_depurar
