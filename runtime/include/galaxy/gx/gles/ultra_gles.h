// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// ULTRA-GLES: the GLES port's measurement loop (docs/RG35XXH.md, "ULTRA-GLES").
//
// GALAXY_ULTRA=1 (or bench) records every presented frame: the time since the previous present, the render thread's
// CPU time and waits, the GPU time (its timer queries, which arrive a few frames late), draws, program changes, uber
// and skipped draws, programs compiled on the render thread, texture uploads, EFB copies and peeks. A writer thread
// (so the files never stall the render thread) appends frames.csv and memoria.csv once a second and, at the end,
// writes resumen.json (what platform/knulli/smg/smg/ultra/compare.py compares) and informe.txt: per scene (the stage
// the game reports) and in total, FPS, the frame time (mean, p50, p95, p99, max), 1 % low, hitches with their likely
// cause, CPU and GPU time, memory and GL errors. The first GALAXY_ULTRA_WARMUP seconds (3) after a scene starts are its
// load and are left out of its steady figures. GALAXY_ULTRA_DIR: the folder (default ultra/ in the working folder);
// GALAXY_ULTRA_CAPTURE=n (bench: 360): a picture of each scene's n-th steady present (capturas/<scene>.ppm), so two
// runs can be compared by eye and by PSNR. Off, every call below is one predictable branch.

#include <cstdint>
#include <string>

namespace galaxy::gx::ultra {

// Counted where they happen (any thread), taken by the frame that presents next.
enum Counter : unsigned {
    kSyncCompiles = 0,  // programs linked on the render thread while a draw waited
    kSyncCompileNs,
    kUberDraws,         // draws drawn with an uber program while their own one compiles
    kPendingSkips,      // draws left out until their program is linked
    kTextureUploads,
    kTextureUploadBytes,
    kDisplayListCompares,      // cached display lists whose bytes were compared with guest memory
    kDisplayListCompareBytes,
    kDisplayListCompareSkips,  // calls that skipped it (already compared since the last guest writes)
    kCounterCount
};

[[nodiscard]] bool enabled() noexcept;
void add(Counter counter, std::uint64_t value) noexcept;

struct FrameSample {
    std::uint64_t frame_index = 0;  // RendererGles's frame (the GPU time is matched by it)
    std::int64_t present_ns = 0;    // monotonic, after the swap
    std::int64_t cpu_ns = 0;        // the render thread's CPU time since the previous present
    std::int64_t draw_ns = 0;       // of it, in RendererGles::draw
    std::int64_t slot_wait_ns = 0;  // waiting for a frame slot (the GPU behind)
    std::int64_t swap_wait_ns = 0;  // in the swap (vsync)
    std::int64_t readback_ns = 0;   // waiting for EFB readbacks
    std::int64_t sync_peek_ns = 0;
    std::uint32_t draws = 0;
    std::uint32_t program_changes = 0;
    std::uint32_t skipped_draws = 0;  // draws without a pipeline
    std::uint32_t copies = 0;
    std::uint32_t peeks = 0;
    std::uint32_t sync_peeks = 0;
    std::uint32_t vertices = 0;
    std::uint32_t repeats = 0;       // display copies repeated: a frame the frame skip left undrawn
    std::uint32_t efb_width = 0;     // the EFB's size this frame (dynamic resolution)
    std::uint32_t efb_height = 0;
    std::uint32_t gl_errors = 0;
    // GALAXY_GLES_THREAD: the GL thread's CPU time since the previous present, the render thread's waits for it
    // (calls that needed an answer, a full queue) and the calls queued.
    bool gl_thread = false;
    std::int64_t gl_cpu_ns = 0;
    std::int64_t gl_sync_wait_ns = 0;
    std::uint32_t gl_syncs = 0;
    std::uint32_t gl_commands = 0;
    // The GL thread's fences (frame-slot waits): slot_wait_ns split into "fence not yet inserted by the GL thread"
    // and "inserted, not yet published"; the mean submit->insert and insert->publish of the fences checked at the
    // frames' starts (fences_timed of them); and the GL thread's swap time with fences unpublished.
    std::int64_t slot_wait_queue_ns = 0;
    std::int64_t slot_wait_gpu_ns = 0;
    std::int64_t fence_queue_ns = 0;
    std::int64_t fence_gpu_ns = 0;
    std::uint32_t fences_timed = 0;
    // Those fences' life split by a GPU timestamp issued just before each: insert->the GPU there, then ->published
    // (fences_gpu_timed of them; fences_gpu_bad: a timestamp outside the fence's life, left out).
    std::int64_t fence_gpu_end_ns = 0;
    std::int64_t fence_publish_ns = 0;
    std::uint32_t fences_gpu_timed = 0;
    std::uint32_t fences_gpu_bad = 0;
    std::int64_t gl_swap_pending_ns = 0;
};

// The render thread, after each present.
void frame(const FrameSample& sample) noexcept;
// The render thread, when a frame's timer queries are read (a few frames later).
void gpu(std::uint64_t frame_index, double total_ms) noexcept;
// The stage the game reports before each 3D scene (host_main.cpp's svc_game_stage).
void set_scene(const char* name) noexcept;
// A picture of the current scene at the next present (the host asks at a fixed game frame of each tour stop).
void request_capture() noexcept;
// GALAXY_ULTRA_CAPTURE: the path to write this present's picture to, or empty (gx_backend's present snapshots).
[[nodiscard]] std::string capture_path();
// The run's end (the renderer's shutdown, and at exit): the last frames, resumen.json and informe.txt. Idempotent.
void finish() noexcept;

}  // namespace galaxy::gx::ultra
