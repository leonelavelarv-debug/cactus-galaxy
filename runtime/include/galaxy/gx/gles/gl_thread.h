// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The GL thread (GALAXY_GLES_THREAD=1, docs/RG35XXH.md): the render thread translates GX and records its GL calls
// into a queue; a second thread, which holds the context, makes them. The driver's own time (a quarter of the render
// thread's on the RG35XX H's Mali-G31) moves to another core while the next commands are translated.
//
// Every entry point in gl_api.h is wrapped once, when the context loads (install): on the recording thread a call is
// queued, everywhere else (the pipeline cache's workers, the start-up before the thread runs) it goes straight to the
// driver. A queued call copies its arguments; one that reads caller memory copies that memory (texture uploads,
// deletes, clear values), and one that returns something or writes through a pointer waits for the GL thread to make
// it (a "sync": the queue before it drains first, so the render thread stalls for it). The steady state makes none:
// names come from pools the GL thread refills, glGetError answers GL_NO_ERROR and the GL thread counts and logs what it
// finds (take_errors), a framebuffer's completeness is checked on the GL thread and logged, and the fences, timer
// queries and readbacks the renderer needs each frame are done on the GL thread (fence values, results posted back).
//
// The CPU-visible rings stay safe: a ring segment is written again only after its frame's fence passed, and the GL
// thread inserts that fence after the frame's last command.

#include "galaxy/gx/gles/gl_api.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace galaxy::gx::gles::glthread {

// The driver's entry points themselves (what gl:: held before install).
namespace real {
#define GLES_THREAD_DECLARE(type, name) extern type name;
GLES_CORE_FUNCTIONS(GLES_THREAD_DECLARE)
GLES_OPTIONAL_FUNCTIONS(GLES_THREAD_DECLARE)
#undef GLES_THREAD_DECLARE
}  // namespace real

// GALAXY_GLES_THREAD=1: the wrappers go in (Context::initialize calls it after loading GL, before anything else
// runs GL). They pass every call straight through until start().
void install();
[[nodiscard]] bool installed() noexcept;

// The GL thread takes the context (the caller has released it, gles_make_current(false)) and this thread starts
// recording. `make_current` runs on the GL thread (true) and when it stops (false). The fences already inserted
// (`pending`, values over `completed`) go on being polled there.
struct PendingFence {
    std::uint64_t value;
    GLsync sync;
};
bool start(std::function<bool(bool)> make_current, std::uint64_t completed, std::vector<PendingFence> pending);
// Everything queued is made, the GL thread releases the context and ends; this thread no longer records. Any thread.
void stop();
[[nodiscard]] bool active() noexcept;
// This thread records into the queue (the one that started it, while it runs).
[[nodiscard]] bool recording() noexcept;

// Queued work of the renderer's own (on the GL thread, in order with the calls around it). `run` may call GL.
void enqueue(std::function<void()> run);
// The same, and waits until it has run.
void run_sync(const std::function<void()>& run);
// OpenGL ES 3.0 buffers with a CPU copy (Context::flush_writes): `size` bytes of `source` into `buffer` at `offset`,
// mapped unsynchronized. `source` must stay as it is until the GPU has read the range (the copy of a ring slot or
// a resident block does: it is not rewritten before the frames that use it are done), and no draw in flight may
// read the range. On the GL thread it is read when the call runs.
void write_buffer(GLuint buffer, std::uint64_t offset, std::uint64_t size, const std::byte* source);
// The GL thread starts on what is queued now (a frame's end; queued calls are also picked up as they come).
void kick() noexcept;

// Fences (Context::submit / completed_value / wait while the thread runs): `value` is inserted after the commands
// queued so far; completed() is the highest value the GPU has passed (the GL thread polls them).
void submit_fence(std::uint64_t value);
[[nodiscard]] std::uint64_t completed() noexcept;
bool wait_fence(std::uint64_t value, std::uint64_t timeout_ns);
// `run` on the GL thread once the GPU has passed `value`, before completed() reports it (a readback's copy).
void after_fence(std::uint64_t value, std::function<void()> run);
// ULTRA-GLES: when the GL thread reached fence `value` (inserted it), when it saw it passed (published it) and, with
// GALAXY_ULTRA, when the GPU reached it (a GL_TIMESTAMP_EXT query issued before the fence, on steady_clock);
// steady_clock ns; 0 when not known (or when the value is too old: the last 16 fences are kept).
void fence_times(std::uint64_t value, std::int64_t& inserted_ns, std::int64_t& published_ns,
                 std::int64_t& gpu_done_ns) noexcept;
// Fences published before their GPU timestamp was available (counted, not timed).
[[nodiscard]] std::uint64_t timestamp_misses() noexcept;
// ULTRA-GLES (u109): what the stretch of commands ending at fence `value` held -- draws, EFB copies, clears, EFB
// reads and whether it presented -- for fences.csv. The render thread, before it submits that fence.
void tag_fence(std::uint64_t value, std::uint32_t draws, std::uint32_t copies, std::uint32_t clears,
               std::uint32_t reads, bool present) noexcept;

// What the render thread waited for (ULTRA-GLES): calls that needed an answer, the time spent waiting for them and
// for room in the queue, and the GL thread's CPU time and its time in the swap (vsync).
struct Stats {
    std::uint64_t syncs = 0;
    std::uint64_t sync_wait_ns = 0;
    std::uint64_t full_wait_ns = 0;
    std::uint64_t gl_cpu_ns = 0;
    std::uint64_t swap_ns = 0;
    std::uint64_t swap_pending_ns = 0;  // of swap_ns, with fences still unpublished (they cannot be polled then)
    std::uint64_t commands = 0;
    std::uint64_t pool_misses = 0;  // names that had to be made at once (an empty pool)
};
[[nodiscard]] Stats stats() noexcept;
// The GL thread's own swap time (it adds to it).
void add_swap_ns(std::uint64_t ns) noexcept;
// GL errors the GL thread found when the render thread asked (glGetError returns GL_NO_ERROR there): taken.
[[nodiscard]] std::uint32_t take_errors() noexcept;

// Tests: the queue without a context (no GL): `run` items in order on the thread, fences complete at once.
bool start_for_tests();

}  // namespace galaxy::gx::gles::glthread
