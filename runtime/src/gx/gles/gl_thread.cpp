// SPDX-License-Identifier: GPL-3.0-only
// The GL thread (gl_thread.h): the command queue, the thread that makes the calls, and the wrappers of every entry point.

#include "galaxy/gx/gles/gl_thread.h"
#include "galaxy/gx/gles/gles_context.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <thread>
#include <type_traits>
#include <utility>

#if !defined(_WIN32)
#include <pthread.h>
#endif

namespace galaxy::gx::gles::glthread {

namespace real {
#define GLES_THREAD_DEFINE(type, name) type name = nullptr;
GLES_CORE_FUNCTIONS(GLES_THREAD_DEFINE)
GLES_OPTIONAL_FUNCTIONS(GLES_THREAD_DEFINE)
#undef GLES_THREAD_DEFINE
}  // namespace real

namespace {

std::int64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

// ---- The queue: one producer (the recording thread), one consumer (the GL thread). Records are a header and a
// payload, 16-byte aligned, in a ring; one that does not fit before the end leaves a wrap marker there.

struct alignas(16) Header {
    void (*run)(Header*) = nullptr;  // null: a wrap marker (skip to the start)
    std::uint32_t size = 0;          // the whole record, header included
};
static_assert(sizeof(Header) == 16);

constexpr std::size_t align16(std::size_t n) { return (n + 15u) & ~std::size_t{15}; }
inline void* payload(Header* h) { return reinterpret_cast<std::byte*>(h) + sizeof(Header); }

class Queue {
public:
    static constexpr std::size_t kCapacity = std::size_t{8} << 20;  // 8 MiB: a frame's calls are ~0.2-0.5 MiB
    static constexpr std::size_t kInlineDataMax = kCapacity / 8u;

    Queue() : data_(static_cast<std::byte*>(::operator new(kCapacity, std::align_val_t{16}))) {}
    ~Queue() { ::operator delete(data_, std::align_val_t{16}); }

    // Producer: room for a record of `payload_bytes`; commit() publishes it.
    Header* reserve(std::size_t payload_bytes) {
        const std::size_t total = sizeof(Header) + align16(payload_bytes);
        std::size_t index = tail_ % kCapacity;
        if (index + total > kCapacity) {
            const std::size_t pad = kCapacity - index;
            wait_room(pad + total);
            auto* wrap = new (data_ + index) Header{};
            wrap->size = static_cast<std::uint32_t>(pad);
            tail_ += pad;
            tail_published_.store(tail_, std::memory_order_release);
            index = 0;
        } else {
            wait_room(total);
        }
        auto* header = new (data_ + index) Header{};
        header->size = static_cast<std::uint32_t>(total);
        return header;
    }
    void commit(Header* header) {
        tail_ += header->size;
        tail_published_.store(tail_, std::memory_order_seq_cst);  // ordered before the sleeping check below
        ++pushed_;
        if (consumer_sleeping_.load(std::memory_order_seq_cst)) {
            wake_consumer();
        }
    }

    // Consumer: runs the next record; false when empty.
    bool run_one() {
        const std::size_t tail = tail_published_.load(std::memory_order_acquire);
        if (head_ == tail) {
            return false;
        }
        auto* header = reinterpret_cast<Header*>(data_ + head_ % kCapacity);
        const std::uint32_t size = header->size;
        if (header->run != nullptr) {
            header->run(header);
        }
        head_ += size;
        head_published_.store(head_, std::memory_order_seq_cst);  // ordered before the waiting check below
        if (producer_waiting_.load(std::memory_order_seq_cst)) {
            std::lock_guard<std::mutex> lock(mutex_);
            room_.notify_one();
        }
        return true;
    }
    [[nodiscard]] bool empty() const noexcept {
        return head_published_.load(std::memory_order_acquire) == tail_published_.load(std::memory_order_acquire);
    }

    // The consumer sleeps until a record comes (or `timeout`); the producer's commit wakes it.
    void sleep(std::chrono::microseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        consumer_sleeping_.store(true, std::memory_order_seq_cst);
        if (head_ == tail_published_.load(std::memory_order_seq_cst) && !stop_) {
            if (timeout.count() > 0) {
                data_ready_.wait_for(lock, timeout);
            } else {
                data_ready_.wait(lock);
            }
        }
        consumer_sleeping_.store(false, std::memory_order_relaxed);
    }
    void wake_consumer() {
        std::lock_guard<std::mutex> lock(mutex_);
        data_ready_.notify_one();
    }
    void request_stop() {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
        data_ready_.notify_one();
    }
    [[nodiscard]] bool stop_requested() {
        std::lock_guard<std::mutex> lock(mutex_);
        return stop_;
    }
    void reset_stop() {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = false;
    }

    std::uint64_t full_wait_ns = 0;  // producer
    std::uint64_t pushed_ = 0;       // producer

private:
    void wait_room(std::size_t bytes) {
        if (tail_ + bytes - head_published_.load(std::memory_order_acquire) <= kCapacity) {
            return;
        }
        const std::int64_t start = now_ns();
        std::unique_lock<std::mutex> lock(mutex_);
        producer_waiting_.store(true, std::memory_order_seq_cst);
        data_ready_.notify_one();
        room_.wait(lock, [&] {
            return tail_ + bytes - head_published_.load(std::memory_order_seq_cst) <= kCapacity;
        });
        producer_waiting_.store(false, std::memory_order_relaxed);
        full_wait_ns += static_cast<std::uint64_t>(now_ns() - start);
    }

    std::byte* data_;
    std::size_t tail_ = 0;  // producer's
    std::size_t head_ = 0;  // consumer's
    alignas(64) std::atomic<std::size_t> tail_published_{0};
    alignas(64) std::atomic<std::size_t> head_published_{0};
    std::atomic<bool> consumer_sleeping_{false};
    std::atomic<bool> producer_waiting_{false};
    std::mutex mutex_;
    std::condition_variable data_ready_;
    std::condition_variable room_;
    bool stop_ = false;
};

// Never destroyed: a process may exit with the GL thread still waiting on it (no renderer shutdown).
Queue& queue() {
    static Queue* q = new Queue;
    return *q;
}

template <class F>
void push(F&& f) {
    using T = std::decay_t<F>;
    Header* header = queue().reserve(sizeof(T));
    new (payload(header)) T(std::forward<F>(f));
    header->run = [](Header* h) {
        T* t = static_cast<T*>(payload(h));
        (*t)();
        t->~T();
    };
    queue().commit(header);
}

// `f(const std::byte* data)` with `bytes` of `data` copied after it in the record (large copies go to the heap).
template <class F>
void push_data(F&& f, const void* data, std::size_t bytes) {
    using T = std::decay_t<F>;
    if (bytes > Queue::kInlineDataMax) {
        std::vector<std::byte> copy(static_cast<const std::byte*>(data), static_cast<const std::byte*>(data) + bytes);
        push([f = std::forward<F>(f), copy = std::move(copy)]() mutable { f(copy.data()); });
        return;
    }
    Header* header = queue().reserve(align16(sizeof(T)) + bytes);
    new (payload(header)) T(std::forward<F>(f));
    if (bytes != 0u) {
        std::memcpy(static_cast<std::byte*>(payload(header)) + align16(sizeof(T)), data, bytes);
    }
    header->run = [](Header* h) {
        T* t = static_cast<T*>(payload(h));
        (*t)(static_cast<const std::byte*>(payload(h)) + align16(sizeof(T)));
        t->~T();
    };
    queue().commit(header);
}

// ---- The thread and its state.

std::atomic<bool> g_installed{false};
std::atomic<bool> g_active{false};
thread_local bool t_recording = false;
// The calling thread records now (a thread that recorded before stop() goes straight to the driver again).
inline bool rec() noexcept { return t_recording && g_active.load(std::memory_order_relaxed); }
std::thread* g_thread = nullptr;  // never destroyed (see queue())
std::function<bool(bool)> g_make_current;
bool g_tests = false;  // no context: fences complete when reached

std::mutex g_start_mutex;
std::condition_variable g_started_cv;
int g_started = 0;  // 1 running, -1 failed

// Producer-side stats.
std::uint64_t g_syncs = 0;
std::uint64_t g_sync_wait_ns = 0;
std::uint64_t g_pool_misses = 0;
// GL-thread-side.
std::atomic<std::uint64_t> g_gl_cpu_ns{0};
std::atomic<std::uint64_t> g_swap_ns{0};
std::atomic<std::uint32_t> g_errors{0};

// Fences: polled by the GL thread.
std::vector<PendingFence> g_pending;  // GL thread
struct Job {
    std::uint64_t value;
    std::function<void()> run;
};
std::vector<Job> g_jobs;  // GL thread
// GALAXY_GLES_THREAD_JOBS=frame: a fence with work after it (a readback's copy, which maps a buffer) is reported only
// between frames (right after a frame's fence was inserted), never while a frame is half recorded: a map there may
// make the driver flush the commands so far. GALAXY_GLES_THREAD_WAIT=poll: with nothing to do, the GL thread sleeps
// and looks again instead of blocking in glClientWaitSync. (Measured against the defaults in the series.)
bool g_jobs_at_boundary = false;
bool g_wait_poll = false;
bool g_at_boundary = true;      // GL thread: the last record run was a fence's insertion
bool g_ran_submit = false;      // GL thread: set by that record

bool has_job_for(std::uint64_t value) {
    for (const Job& job : g_jobs) {
        if (job.value <= value) return true;
    }
    return false;
}
std::atomic<std::uint64_t> g_completed{0};
std::mutex g_fence_mutex;
std::condition_variable g_fence_cv;
std::atomic<int> g_fence_waiters{0};

void publish_completed(std::uint64_t value) {
    // Jobs for this value first: completed() promises their results.
    for (std::size_t i = 0; i < g_jobs.size();) {
        if (g_jobs[i].value <= value) {
            g_jobs[i].run();
            g_jobs.erase(g_jobs.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            ++i;
        }
    }
    g_completed.store(value, std::memory_order_release);
    if (g_fence_waiters.load(std::memory_order_seq_cst) != 0) {
        std::lock_guard<std::mutex> lock(g_fence_mutex);
        g_fence_cv.notify_all();
    }
}

// The fences the GPU has passed, oldest first; `wait_ns`: block that long on the oldest.
void poll_fences(std::uint64_t wait_ns) {
    while (!g_pending.empty()) {
        const PendingFence& front = g_pending.front();
        // Never GL_SYNC_FLUSH_COMMANDS_BIT: every fence was flushed when it was inserted, and a flush here would land
        // in the middle of the next frame's commands (the GL thread waits while the render thread is still
        // translating). On a tiled GPU that splits the render pass: the EFB is stored and loaded again, and the Mali's
        // time per frame went up by 40 % (update49's series, the copies part three times over).
        const GLenum status = real::glClientWaitSync(front.sync, 0u, wait_ns);
        if (status != GL_ALREADY_SIGNALED && status != GL_CONDITION_SATISFIED) {
            if (status == GL_WAIT_FAILED) {
                // A lost sync must not hold every later frame back.
                std::fprintf(stderr, "[gl-thread] fence %llu: wait failed\n",
                             static_cast<unsigned long long>(front.value));
            } else {
                return;
            }
        }
        const std::uint64_t value = front.value;
        if (g_jobs_at_boundary && !g_at_boundary && has_job_for(value)) {
            return;  // passed, but its copy waits for the end of the frame being recorded
        }
        real::glDeleteSync(front.sync);
        g_pending.erase(g_pending.begin());
        publish_completed(value);
        wait_ns = 0u;
    }
}

void update_cpu() { g_gl_cpu_ns.store(static_cast<std::uint64_t>(thread_cpu_ns()), std::memory_order_relaxed); }

void thread_main() {
#if !defined(_WIN32)
    pthread_setname_np(pthread_self(), "Nebula GL");
#endif
    const bool current = g_tests || (g_make_current && g_make_current(true));
    {
        std::lock_guard<std::mutex> lock(g_start_mutex);
        g_started = current ? 1 : -1;
    }
    g_started_cv.notify_all();
    if (!current) {
        return;
    }
    Queue& q = queue();
    std::uint64_t ran = 0;
    for (;;) {
        bool any = false;
        while (q.run_one()) {
            any = true;
            g_at_boundary = g_ran_submit;
            g_ran_submit = false;
            if (((++ran & 31u) == 0u || g_at_boundary) && !g_tests) {
                poll_fences(0u);  // and right after each frame's fence, when the copies may run
            }
            if ((ran & 255u) == 0u) {
                update_cpu();
            }
        }
        if (!g_tests) {
            poll_fences(0u);
        }
        if (any) {
            update_cpu();
            // A moment for the next record before sleeping (the producer is usually mid-frame).
            for (int spin = 0; spin < 64 && q.empty(); ++spin) {
                std::this_thread::yield();
            }
            continue;
        }
        if (q.stop_requested() && q.empty()) {
            break;
        }
        if (!g_pending.empty()) {
            // The GPU still has frames: wait on the oldest a little, then look at the queue again.
            if (g_wait_poll) {
                q.sleep(std::chrono::microseconds(250));
            } else {
                poll_fences(200'000u);
                if (!g_pending.empty() && q.empty()) {
                    q.sleep(std::chrono::microseconds(300));
                }
            }
        } else {
            q.sleep(std::chrono::microseconds(0));
        }
    }
    // Everything is made: let the GPU finish, and every fence counts as passed.
    if (!g_tests) {
        real::glFinish();
        while (!g_pending.empty()) {
            real::glDeleteSync(g_pending.front().sync);
            const std::uint64_t value = g_pending.front().value;
            g_pending.erase(g_pending.begin());
            publish_completed(value);
        }
    }
    for (Job& job : g_jobs) {
        job.run();
    }
    g_jobs.clear();
    update_cpu();
    if (g_make_current) {
        g_make_current(false);
    }
}

// The recording thread waits for `run` to have been made on the GL thread (after everything queued before it).
void sync_call(const std::function<void()>& run) {
    struct Done {
        std::mutex mutex;
        std::condition_variable cv;
        bool done = false;
    } done;
    const std::int64_t start = now_ns();
    const std::function<void()>* call = &run;
    Done* d = &done;
    push([call, d] {
        (*call)();
        std::lock_guard<std::mutex> lock(d->mutex);
        d->done = true;
        d->cv.notify_one();
    });
    queue().wake_consumer();
    {
        std::unique_lock<std::mutex> lock(done.mutex);
        done.cv.wait(lock, [&] { return done.done; });
    }
    ++g_syncs;
    g_sync_wait_ns += static_cast<std::uint64_t>(now_ns() - start);
}

// ---- Shadow state the wrappers need: what the GL thread will have bound when it reaches the call.

struct Shadow {
    GLuint pack_buffer = 0;
    GLuint unpack_buffer = 0;
    GLint unpack_alignment = 4;
    GLint unpack_row_length = 0;
    GLint unpack_skip_rows = 0;
    GLint unpack_skip_pixels = 0;
} g_shadow;  // the recording thread's

// ---- Name pools (the GL thread refills them).

enum PoolKind { kPoolTextures = 0, kPoolFramebuffers, kPoolBuffers, kPoolSamplers, kPoolCount };
struct Pool {
    std::mutex mutex;
    std::vector<GLuint> names;
    bool refilling = false;
};
std::array<Pool, kPoolCount> g_pools;
constexpr std::size_t kPoolRefill = 48u;
constexpr std::size_t kPoolLow = 16u;

void real_gen(PoolKind kind, GLsizei n, GLuint* out) {
    switch (kind) {
    case kPoolTextures: real::glGenTextures(n, out); break;
    case kPoolFramebuffers: real::glGenFramebuffers(n, out); break;
    case kPoolBuffers: real::glGenBuffers(n, out); break;
    case kPoolSamplers: real::glGenSamplers(n, out); break;
    default: break;
    }
}

void refill(PoolKind kind) {
    std::array<GLuint, kPoolRefill> names{};
    real_gen(kind, static_cast<GLsizei>(names.size()), names.data());
    Pool& pool = g_pools[kind];
    std::lock_guard<std::mutex> lock(pool.mutex);
    for (const GLuint name : names) {
        if (name != 0u) pool.names.push_back(name);
    }
    pool.refilling = false;
}

void pooled_gen(PoolKind kind, GLsizei n, GLuint* out) {
    if (n <= 0) {
        return;
    }
    GLsizei have = 0;
    bool want_refill = false;
    {
        Pool& pool = g_pools[kind];
        std::lock_guard<std::mutex> lock(pool.mutex);
        while (have < n && !pool.names.empty()) {
            out[have++] = pool.names.back();
            pool.names.pop_back();
        }
        if (pool.names.size() < kPoolLow && !pool.refilling) {
            pool.refilling = true;
            want_refill = true;
        }
    }
    if (want_refill) {
        push([kind] { refill(kind); });
    }
    if (have < n) {
        g_pool_misses += static_cast<std::uint64_t>(n - have);
        GLuint* rest = out + have;
        const GLsizei count = n - have;
        sync_call([kind, rest, count] { real_gen(kind, count, rest); });
    }
}

// ---- The wrappers.

template <class T>
constexpr bool kIsPointer = std::is_pointer_v<T>;

// Generic: a void call of plain values is queued; anything that returns a value or takes a pointer waits for it.
template <auto* Real, typename Sig>
struct Thunk;
template <auto* Real, typename R, typename... A>
struct Thunk<Real, R(A...)> {
    static R GL_APIENTRY call(A... a) {
        if (!rec()) {
            return (*Real)(a...);
        }
        if constexpr (std::is_void_v<R> && !(kIsPointer<A> || ...)) {
            push([=] { (*Real)(a...); });
        } else if constexpr (std::is_void_v<R>) {
            sync_call([&] { (*Real)(a...); });
        } else {
            R result{};
            sync_call([&] { result = (*Real)(a...); });
            return result;
        }
    }
};

// Queued although they take a pointer: an offset into the bound buffer.
template <auto* Real, typename Sig>
struct OffsetThunk;
template <auto* Real, typename... A>
struct OffsetThunk<Real, void(A...)> {
    static void GL_APIENTRY call(A... a) {
        if (!rec()) {
            (*Real)(a...);
            return;
        }
        push([=] { (*Real)(a...); });
    }
};

void GL_APIENTRY w_glFinish() {
    if (!rec()) {
        real::glFinish();
        return;
    }
    sync_call([] { real::glFinish(); });
}

void GL_APIENTRY w_glBindBuffer(GLenum target, GLuint buffer) {
    if (!rec()) {
        real::glBindBuffer(target, buffer);
        return;
    }
    if (target == GL_PIXEL_PACK_BUFFER) g_shadow.pack_buffer = buffer;
    if (target == GL_PIXEL_UNPACK_BUFFER) g_shadow.unpack_buffer = buffer;
    push([=] { real::glBindBuffer(target, buffer); });
}

void GL_APIENTRY w_glPixelStorei(GLenum pname, GLint param) {
    if (!rec()) {
        real::glPixelStorei(pname, param);
        return;
    }
    switch (pname) {
    case GL_UNPACK_ALIGNMENT: g_shadow.unpack_alignment = param; break;
    case GL_UNPACK_ROW_LENGTH: g_shadow.unpack_row_length = param; break;
    case GL_UNPACK_SKIP_ROWS: g_shadow.unpack_skip_rows = param; break;
    case GL_UNPACK_SKIP_PIXELS: g_shadow.unpack_skip_pixels = param; break;
    default: break;
    }
    push([=] { real::glPixelStorei(pname, param); });
}

// The bytes a client-memory upload reads (0: not one this knows; the call then waits instead).
std::size_t upload_bytes(GLsizei width, GLsizei height, GLenum format, GLenum type) {
    std::size_t texel = 0;
    if (type == GL_UNSIGNED_BYTE) {
        texel = format == GL_RGBA ? 4u : format == GL_RGB ? 3u : (format == GL_RED || format == GL_ALPHA ||
                                                                  format == GL_LUMINANCE) ? 1u
                                                               : format == GL_RG ? 2u : 0u;
    } else if ((type == GL_UNSIGNED_SHORT_5_6_5 && format == GL_RGB) ||
               ((type == GL_UNSIGNED_SHORT_5_5_5_1 || type == GL_UNSIGNED_SHORT_4_4_4_4) && format == GL_RGBA)) {
        texel = 2u;  // compact textures (texture_compact.h)
    } else if ((type == GL_FLOAT && format == GL_RGBA) || (type == GL_UNSIGNED_INT && format == GL_RGBA_INTEGER)) {
        texel = 16u;  // GL_RGBA_INTEGER: the matrix palette's 2D texture (OpenGL ES 3.0, renderer_gles.cpp)
    }
    if (texel == 0u || width <= 0 || height <= 0 || g_shadow.unpack_skip_rows != 0 || g_shadow.unpack_skip_pixels != 0) {
        return 0u;
    }
    const std::size_t row_pixels = g_shadow.unpack_row_length > 0 ? static_cast<std::size_t>(g_shadow.unpack_row_length)
                                                                   : static_cast<std::size_t>(width);
    const std::size_t align = static_cast<std::size_t>(std::max(1, g_shadow.unpack_alignment));
    const std::size_t stride = (row_pixels * texel + align - 1u) / align * align;
    return stride * static_cast<std::size_t>(height - 1) + static_cast<std::size_t>(width) * texel;
}

void GL_APIENTRY w_glTexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei width, GLsizei height,
                                   GLenum format, GLenum type, const void* pixels) {
    if (!rec()) {
        real::glTexSubImage2D(target, level, x, y, width, height, format, type, pixels);
        return;
    }
    if (g_shadow.unpack_buffer != 0u || pixels == nullptr) {
        push([=] { real::glTexSubImage2D(target, level, x, y, width, height, format, type, pixels); });
        return;
    }
    const std::size_t bytes = upload_bytes(width, height, format, type);
    if (bytes == 0u) {
        sync_call([&] { real::glTexSubImage2D(target, level, x, y, width, height, format, type, pixels); });
        return;
    }
    push_data([=](const std::byte* data) {
        real::glTexSubImage2D(target, level, x, y, width, height, format, type, data);
    }, pixels, bytes);
}

void GL_APIENTRY w_glTexImage2D(GLenum target, GLint level, GLint internal, GLsizei width, GLsizei height,
                                GLint border, GLenum format, GLenum type, const void* pixels) {
    if (!rec()) {
        real::glTexImage2D(target, level, internal, width, height, border, format, type, pixels);
        return;
    }
    if (g_shadow.unpack_buffer != 0u || pixels == nullptr) {
        push([=] { real::glTexImage2D(target, level, internal, width, height, border, format, type, pixels); });
        return;
    }
    const std::size_t bytes = upload_bytes(width, height, format, type);
    if (bytes == 0u) {
        sync_call([&] { real::glTexImage2D(target, level, internal, width, height, border, format, type, pixels); });
        return;
    }
    push_data([=](const std::byte* data) {
        real::glTexImage2D(target, level, internal, width, height, border, format, type, data);
    }, pixels, bytes);
}

void GL_APIENTRY w_glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void* data) {
    if (!rec()) {
        real::glBufferSubData(target, offset, size, data);
        return;
    }
    push_data([=](const std::byte* bytes) { real::glBufferSubData(target, offset, size, bytes); }, data,
              static_cast<std::size_t>(size));
}

void GL_APIENTRY w_glBufferData(GLenum target, GLsizeiptr size, const void* data, GLenum usage) {
    if (!rec()) {
        real::glBufferData(target, size, data, usage);
        return;
    }
    if (data == nullptr) {
        push([=] { real::glBufferData(target, size, nullptr, usage); });
        return;
    }
    push_data([=](const std::byte* bytes) { real::glBufferData(target, size, bytes, usage); }, data,
              static_cast<std::size_t>(size));
}

// glDelete*(n, names), glDrawBuffers(n, bufs) and the like: the array copied.
template <auto* Real>
struct ArrayThunk {
    static void GL_APIENTRY call(GLsizei n, const GLuint* names) {
        if (!rec()) {
            (*Real)(n, names);
            return;
        }
        if (n <= 0) {
            return;
        }
        push_data([n](const std::byte* data) { (*Real)(n, reinterpret_cast<const GLuint*>(data)); }, names,
                  static_cast<std::size_t>(n) * sizeof(GLuint));
    }
};

void GL_APIENTRY w_glDrawBuffers(GLsizei n, const GLenum* bufs) {
    if (!rec()) {
        real::glDrawBuffers(n, bufs);
        return;
    }
    push_data([n](const std::byte* data) { real::glDrawBuffers(n, reinterpret_cast<const GLenum*>(data)); }, bufs,
              static_cast<std::size_t>(std::max<GLsizei>(n, 0)) * sizeof(GLenum));
}

void GL_APIENTRY w_glInvalidateFramebuffer(GLenum target, GLsizei n, const GLenum* attachments) {
    if (!rec()) {
        real::glInvalidateFramebuffer(target, n, attachments);
        return;
    }
    push_data([target, n](const std::byte* data) {
        real::glInvalidateFramebuffer(target, n, reinterpret_cast<const GLenum*>(data));
    }, attachments, static_cast<std::size_t>(std::max<GLsizei>(n, 0)) * sizeof(GLenum));
}

void GL_APIENTRY w_glClearBufferfv(GLenum buffer, GLint drawbuffer, const GLfloat* value) {
    if (!rec()) {
        real::glClearBufferfv(buffer, drawbuffer, value);
        return;
    }
    const std::size_t count = buffer == GL_COLOR ? 4u : 1u;
    push_data([buffer, drawbuffer](const std::byte* data) {
        real::glClearBufferfv(buffer, drawbuffer, reinterpret_cast<const GLfloat*>(data));
    }, value, count * sizeof(GLfloat));
}

void GL_APIENTRY w_glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type,
                                void* pixels) {
    if (!rec()) {
        real::glReadPixels(x, y, width, height, format, type, pixels);
        return;
    }
    if (g_shadow.pack_buffer != 0u) {
        push([=] { real::glReadPixels(x, y, width, height, format, type, pixels); });  // into the bound buffer
        return;
    }
    sync_call([&] { real::glReadPixels(x, y, width, height, format, type, pixels); });
}

// The render thread never needs the answer at once: the errors are counted (and the first logged) on the GL thread.
GLenum GL_APIENTRY w_glGetError() {
    if (!rec()) {
        return real::glGetError();
    }
    push([] {
        for (int i = 0; i < 8; ++i) {
            const GLenum error = real::glGetError();
            if (error == GL_NO_ERROR) {
                break;
            }
            if (g_errors.fetch_add(1u, std::memory_order_relaxed) < 16u) {
                std::fprintf(stderr, "[gl-thread] GL error 0x%04x\n", error);
            }
        }
    });
    return GL_NO_ERROR;
}

GLenum GL_APIENTRY w_glCheckFramebufferStatus(GLenum target) {
    if (!rec()) {
        return real::glCheckFramebufferStatus(target);
    }
    push([target] {
        const GLenum status = real::glCheckFramebufferStatus(target);
        static std::atomic<unsigned> reported{0};
        if (status != GL_FRAMEBUFFER_COMPLETE && reported.fetch_add(1u, std::memory_order_relaxed) < 16u) {
            std::fprintf(stderr, "[gl-thread] framebuffer incomplete (0x%04x)\n", status);
        }
    });
    return GL_FRAMEBUFFER_COMPLETE;
}

template <PoolKind Kind, auto* Real>
struct GenThunk {
    static void GL_APIENTRY call(GLsizei n, GLuint* out) {
        if (!rec()) {
            (*Real)(n, out);
            return;
        }
        pooled_gen(Kind, n, out);
    }
};

}  // namespace

// ---------------------------------------------------------------------------------------------------------------------

void install() {
    if (g_installed.exchange(true)) {
        return;
    }
#define GLES_THREAD_SAVE(type, name) real::name = gl::name;
    GLES_CORE_FUNCTIONS(GLES_THREAD_SAVE)
    GLES_OPTIONAL_FUNCTIONS(GLES_THREAD_SAVE)
#undef GLES_THREAD_SAVE
#define GLES_THREAD_WRAP(type, name) \
    if (real::name != nullptr) gl::name = &Thunk<&real::name, std::remove_pointer_t<type>>::call;
    GLES_CORE_FUNCTIONS(GLES_THREAD_WRAP)
    GLES_OPTIONAL_FUNCTIONS(GLES_THREAD_WRAP)
#undef GLES_THREAD_WRAP
    gl::glFinish = w_glFinish;
    gl::glBindBuffer = w_glBindBuffer;
    gl::glPixelStorei = w_glPixelStorei;
    gl::glTexSubImage2D = w_glTexSubImage2D;
    gl::glTexImage2D = w_glTexImage2D;
    gl::glBufferSubData = w_glBufferSubData;
    gl::glBufferData = w_glBufferData;
    gl::glDeleteTextures = ArrayThunk<&real::glDeleteTextures>::call;
    gl::glDeleteBuffers = ArrayThunk<&real::glDeleteBuffers>::call;
    gl::glDeleteFramebuffers = ArrayThunk<&real::glDeleteFramebuffers>::call;
    gl::glDeleteSamplers = ArrayThunk<&real::glDeleteSamplers>::call;
    gl::glDeleteVertexArrays = ArrayThunk<&real::glDeleteVertexArrays>::call;
    if (real::glDeleteQueries != nullptr) {
        gl::glDeleteQueries = ArrayThunk<&real::glDeleteQueries>::call;
    }
    gl::glDrawBuffers = w_glDrawBuffers;
    gl::glInvalidateFramebuffer = w_glInvalidateFramebuffer;
    gl::glClearBufferfv = w_glClearBufferfv;
    gl::glReadPixels = w_glReadPixels;
    gl::glGetError = w_glGetError;
    gl::glCheckFramebufferStatus = w_glCheckFramebufferStatus;
    gl::glDrawElements = OffsetThunk<&real::glDrawElements, std::remove_pointer_t<PFNGLDRAWELEMENTSPROC>>::call;
    gl::glVertexAttribPointer =
        OffsetThunk<&real::glVertexAttribPointer, std::remove_pointer_t<PFNGLVERTEXATTRIBPOINTERPROC>>::call;
    gl::glVertexAttribIPointer =
        OffsetThunk<&real::glVertexAttribIPointer, std::remove_pointer_t<PFNGLVERTEXATTRIBIPOINTERPROC>>::call;
    gl::glGenTextures = GenThunk<kPoolTextures, &real::glGenTextures>::call;
    gl::glGenFramebuffers = GenThunk<kPoolFramebuffers, &real::glGenFramebuffers>::call;
    gl::glGenBuffers = GenThunk<kPoolBuffers, &real::glGenBuffers>::call;
    gl::glGenSamplers = GenThunk<kPoolSamplers, &real::glGenSamplers>::call;
}

bool installed() noexcept { return g_installed.load(std::memory_order_acquire); }

namespace {

bool start_thread(std::function<bool(bool)> make_current, bool tests, std::uint64_t completed,
                  std::vector<PendingFence> pending) {
    if (g_active.load()) {
        return t_recording;
    }
    g_tests = tests;
    if (const char* jobs = std::getenv("GALAXY_GLES_THREAD_JOBS")) g_jobs_at_boundary = std::strcmp(jobs, "frame") == 0;
    if (const char* wait = std::getenv("GALAXY_GLES_THREAD_WAIT")) g_wait_poll = std::strcmp(wait, "poll") == 0;
    g_at_boundary = true;
    g_make_current = std::move(make_current);
    g_pending = std::move(pending);
    g_jobs.clear();
    g_completed.store(completed, std::memory_order_release);
    queue().reset_stop();
    {
        std::lock_guard<std::mutex> lock(g_start_mutex);
        g_started = 0;
    }
    g_thread = new std::thread(thread_main);
    {
        std::unique_lock<std::mutex> lock(g_start_mutex);
        g_started_cv.wait(lock, [] { return g_started != 0; });
    }
    if (g_started < 0) {
        g_thread->join();
        delete g_thread;
        g_thread = nullptr;
        std::fprintf(stderr, "[gl-thread] the context could not be made current on the GL thread\n");
        return false;
    }
    g_active.store(true, std::memory_order_release);
    t_recording = true;
    if (!tests) {
        // What the GL thread has bound and set now (start-up left it), and full name pools.
        GLint pack = 0, unpack = 0, alignment = 4, row = 0, skip_rows = 0, skip_pixels = 0;
        sync_call([&] {
            real::glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &pack);
            real::glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpack);
            real::glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
            real::glGetIntegerv(GL_UNPACK_ROW_LENGTH, &row);
            real::glGetIntegerv(GL_UNPACK_SKIP_ROWS, &skip_rows);
            real::glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &skip_pixels);
            for (unsigned kind = 0; kind < kPoolCount; ++kind) {
                refill(static_cast<PoolKind>(kind));
                refill(static_cast<PoolKind>(kind));
            }
        });
        g_shadow.pack_buffer = static_cast<GLuint>(pack);
        g_shadow.unpack_buffer = static_cast<GLuint>(unpack);
        g_shadow.unpack_alignment = alignment;
        g_shadow.unpack_row_length = row;
        g_shadow.unpack_skip_rows = skip_rows;
        g_shadow.unpack_skip_pixels = skip_pixels;
    }
    std::fprintf(stderr, "[gl-thread] GL calls on their own thread (readback copies %s, idle %s)\n",
                 g_jobs_at_boundary ? "between frames" : "when their fence passes",
                 g_wait_poll ? "sleeps and polls" : "waits on the oldest fence");
    return true;
}

}  // namespace

bool start(std::function<bool(bool)> make_current, std::uint64_t completed, std::vector<PendingFence> pending) {
    if (!installed()) {
        return false;
    }
    return start_thread(std::move(make_current), false, completed, std::move(pending));
}

bool start_for_tests() { return start_thread({}, true, 0u, {}); }

void stop() {
    if (!g_active.load()) {
        return;
    }
    queue().request_stop();
    if (g_thread != nullptr && g_thread->joinable()) {
        g_thread->join();
        delete g_thread;
        g_thread = nullptr;
    }
    g_active.store(false, std::memory_order_release);
    t_recording = false;
    g_make_current = {};
}

bool active() noexcept { return g_active.load(std::memory_order_acquire); }

bool recording() noexcept { return rec(); }

void enqueue(std::function<void()> run) {
    if (!rec()) {
        run();
        return;
    }
    push(std::move(run));
}

void run_sync(const std::function<void()>& run) {
    if (!rec()) {
        run();
        return;
    }
    sync_call(run);
}

void kick() noexcept {
    if (rec()) {
        queue().wake_consumer();
    }
}

void submit_fence(std::uint64_t value) {
    if (g_tests) {
        push([value] { publish_completed(value); });
        return;
    }
    push([value] {
        GLsync sync = real::glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        real::glFlush();
        g_ran_submit = true;
        if (sync == nullptr) {
            publish_completed(value);  // nothing to wait for: a lost fence must not stall the frames
            return;
        }
        g_pending.push_back({value, sync});
    });
    queue().wake_consumer();
}

std::uint64_t completed() noexcept { return g_completed.load(std::memory_order_acquire); }

bool wait_fence(std::uint64_t value, std::uint64_t timeout_ns) {
    if (completed() >= value) {
        return true;
    }
    queue().wake_consumer();
    const std::int64_t start = now_ns();
    g_fence_waiters.fetch_add(1, std::memory_order_seq_cst);
    bool ok = true;
    {
        std::unique_lock<std::mutex> lock(g_fence_mutex);
        const auto done = [&] { return g_completed.load(std::memory_order_acquire) >= value; };
        if (timeout_ns == UINT64_MAX) {
            g_fence_cv.wait(lock, done);
        } else {
            ok = g_fence_cv.wait_for(lock, std::chrono::nanoseconds(timeout_ns), done);
        }
    }
    g_fence_waiters.fetch_sub(1, std::memory_order_seq_cst);
    (void)start;
    return ok;
}

void after_fence(std::uint64_t value, std::function<void()> run) {
    push([value, run = std::move(run)]() mutable {
        if (g_completed.load(std::memory_order_acquire) >= value) {
            run();
        } else {
            g_jobs.push_back({value, std::move(run)});
        }
    });
}

Stats stats() noexcept {
    Stats s;
    s.syncs = g_syncs;
    s.sync_wait_ns = g_sync_wait_ns;
    s.full_wait_ns = queue().full_wait_ns;
    s.gl_cpu_ns = g_gl_cpu_ns.load(std::memory_order_relaxed);
    s.swap_ns = g_swap_ns.load(std::memory_order_relaxed);
    s.commands = queue().pushed_;
    s.pool_misses = g_pool_misses;
    return s;
}

void add_swap_ns(std::uint64_t ns) noexcept { g_swap_ns.fetch_add(ns, std::memory_order_relaxed); }

std::uint32_t take_errors() noexcept { return g_errors.exchange(0u, std::memory_order_relaxed); }

}  // namespace galaxy::gx::gles::glthread
