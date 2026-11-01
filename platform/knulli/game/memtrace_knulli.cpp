// SPDX-License-Identifier: GPL-3.0-only
// NEBULA_MEMTRACE=1: who holds the heap's big blocks (docs/GX_ES_MEMORY_GROWTH.md). The executable's malloc, free,
// calloc, realloc and the aligned variants stand in front of glibc's (__libc_*) for the whole process, game module and
// GL driver included. Blocks of NEBULA_MEMTRACE_MIN_KB (256) or more are charged to their call stack; GxBackend's
// [memoria] line asks for the report (nebula_memtrace_report): the stacks that hold the most, as module+offset, which
// `nopal`/llvm-symbolizer turn into functions with the build's unstripped binaries. Off (the default), every call is one
// load and a branch more than glibc's.
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <malloc.h>
#include <unwind.h>

extern "C" {
void* __libc_malloc(std::size_t);
void __libc_free(void*);
void* __libc_calloc(std::size_t, std::size_t);
void* __libc_realloc(void*, std::size_t);
void* __libc_memalign(std::size_t, std::size_t);
}

namespace {

constexpr unsigned kDepth = 10;     // frames kept per stack
constexpr unsigned kSkip = 2;       // this file's frames
constexpr std::size_t kLive = 1u << 16;   // big blocks alive at once (open addressing)
constexpr std::size_t kSites = 1u << 12;  // distinct stacks

struct Site {
    std::uint64_t hash = 0;
    std::uintptr_t pcs[kDepth]{};
    std::uint64_t live_bytes = 0;
    std::uint64_t peak_bytes = 0;
    std::uint64_t live_blocks = 0;
    std::uint64_t allocated_bytes = 0;  // since the start
};
struct Live {
    std::uintptr_t ptr = 0;  // 0 empty, 1 removed
    std::uint64_t size = 0;
    std::uint32_t site = 0;
};

// Static storage, zero-initialized before any constructor runs (malloc is called that early).
Site g_sites[kSites];
Live g_live[kLive];
std::atomic<int> g_state{0};  // 0 not read yet, 1 off, 2 on
std::size_t g_min = 256u << 10;
std::atomic_flag g_lock = ATOMIC_FLAG_INIT;
std::uint64_t g_dropped = 0;
thread_local bool t_inside = false;

bool enabled() {
    int state = g_state.load(std::memory_order_relaxed);
    if (state == 0) {
        // getenv does not allocate.
        const char* on = std::getenv("NEBULA_MEMTRACE");
        state = on != nullptr && on[0] == '1' ? 2 : 1;
        if (state == 2) {
            if (const char* kb = std::getenv("NEBULA_MEMTRACE_MIN_KB")) {
                const long value = std::strtol(kb, nullptr, 10);
                if (value > 0) g_min = static_cast<std::size_t>(value) << 10;
            }
        }
        g_state.store(state, std::memory_order_relaxed);
    }
    return state == 2;
}

struct Lock {
    Lock() { while (g_lock.test_and_set(std::memory_order_acquire)) {} }
    ~Lock() { g_lock.clear(std::memory_order_release); }
};

struct Walk {
    std::uintptr_t* pcs;
    unsigned count;
    unsigned skip;
};
_Unwind_Reason_Code walk_frame(_Unwind_Context* context, void* opaque) {
    Walk& walk = *static_cast<Walk*>(opaque);
    const std::uintptr_t pc = _Unwind_GetIP(context);
    if (pc == 0u) return _URC_END_OF_STACK;
    if (walk.skip != 0u) {
        --walk.skip;
        return _URC_NO_REASON;
    }
    walk.pcs[walk.count++] = pc;
    return walk.count == kDepth ? _URC_END_OF_STACK : _URC_NO_REASON;
}

std::size_t live_slot(std::uintptr_t ptr) { return (ptr >> 4) * 0x9E3779B97F4A7C15ull >> (64 - 16); }

void record(void* ptr, std::size_t size) {
    if (ptr == nullptr || size < g_min || t_inside) return;
    t_inside = true;
    std::uintptr_t pcs[kDepth]{};
    Walk walk{pcs, 0u, kSkip};
    _Unwind_Backtrace(walk_frame, &walk);
    std::uint64_t hash = 1469598103934665603ull;
    for (unsigned i = 0; i < walk.count; ++i) hash = (hash ^ pcs[i]) * 1099511628211ull;
    {
        Lock lock;
        std::size_t s = hash % kSites;
        for (std::size_t n = 0; n < kSites; ++n, s = (s + 1u) % kSites) {
            if (g_sites[s].hash == hash || g_sites[s].hash == 0u) break;
        }
        Site& site = g_sites[s];
        if (site.hash != hash && site.hash != 0u) {
            ++g_dropped;
        } else {
            if (site.hash == 0u) {
                site.hash = hash;
                std::memcpy(site.pcs, pcs, sizeof(pcs));
            }
            std::size_t l = live_slot(reinterpret_cast<std::uintptr_t>(ptr));
            for (std::size_t n = 0; n < kLive; ++n, l = (l + 1u) & (kLive - 1u)) {
                if (g_live[l].ptr <= 1u) {
                    g_live[l] = {reinterpret_cast<std::uintptr_t>(ptr), size, static_cast<std::uint32_t>(s)};
                    site.live_bytes += size;
                    site.allocated_bytes += size;
                    ++site.live_blocks;
                    if (site.live_bytes > site.peak_bytes) site.peak_bytes = site.live_bytes;
                    break;
                }
            }
        }
    }
    t_inside = false;
}

void forget(void* ptr) {
    if (ptr == nullptr) return;
    Lock lock;
    std::size_t l = live_slot(reinterpret_cast<std::uintptr_t>(ptr));
    for (std::size_t n = 0; n < kLive; ++n, l = (l + 1u) & (kLive - 1u)) {
        if (g_live[l].ptr == 0u) return;  // never seen: a small block
        if (g_live[l].ptr == reinterpret_cast<std::uintptr_t>(ptr)) {
            Site& site = g_sites[g_live[l].site];
            site.live_bytes -= g_live[l].size;
            --site.live_blocks;
            g_live[l].ptr = 1u;
            return;
        }
    }
}

}  // namespace

extern "C" {

void* malloc(std::size_t size) {
    void* ptr = __libc_malloc(size);
    if (enabled()) record(ptr, size);
    return ptr;
}

void free(void* ptr) {
    if (g_state.load(std::memory_order_relaxed) == 2) forget(ptr);
    __libc_free(ptr);
}

void* calloc(std::size_t count, std::size_t size) {
    void* ptr = __libc_calloc(count, size);
    if (enabled() && size != 0u && count <= SIZE_MAX / size) record(ptr, count * size);
    return ptr;
}

void* realloc(void* old, std::size_t size) {
    if (!enabled()) return __libc_realloc(old, size);
    void* ptr = __libc_realloc(old, size);
    if (ptr != nullptr || size == 0u) {
        forget(old);       // moved or freed
        record(ptr, size);
    }
    return ptr;
}

void* memalign(std::size_t alignment, std::size_t size) {
    void* ptr = __libc_memalign(alignment, size);
    if (enabled()) record(ptr, size);
    return ptr;
}

void* aligned_alloc(std::size_t alignment, std::size_t size) { return memalign(alignment, size); }

int posix_memalign(void** out, std::size_t alignment, std::size_t size) {
    if (alignment < sizeof(void*) || (alignment & (alignment - 1u)) != 0u) return 22;  // EINVAL
    void* ptr = memalign(alignment, size);
    if (ptr == nullptr && size != 0u) return 12;  // ENOMEM
    *out = ptr;
    return 0;
}

// The census (GxBackend::log_memory_census) prints this after its [memoria] line.
void nebula_memtrace_report() {
    if (g_state.load(std::memory_order_relaxed) != 2) return;
    // A copy of the biggest holders, so dladdr and printf run without the lock.
    constexpr unsigned kTop = 12;
    Site top[kTop]{};
    std::uint64_t total = 0, blocks = 0, dropped = 0;
    {
        Lock lock;
        for (const Site& site : g_sites) {
            if (site.hash == 0u) continue;
            total += site.live_bytes;
            blocks += site.live_blocks;
            for (unsigned i = 0; i < kTop; ++i) {
                if (site.live_bytes > top[i].live_bytes) {
                    std::memmove(&top[i + 1], &top[i], sizeof(Site) * (kTop - 1 - i));
                    top[i] = site;
                    break;
                }
            }
        }
        dropped = g_dropped;
    }
    t_inside = true;
    std::fprintf(stderr, "[memtrace] bloques de %zu KiB o mas: %.1f MiB en %llu bloques%s\n", g_min >> 10,
                 static_cast<double>(total) / 1048576.0, static_cast<unsigned long long>(blocks),
                 dropped != 0u ? " (tabla de pilas llena: faltan algunas)" : "");
    for (unsigned i = 0; i < kTop && top[i].live_bytes != 0u; ++i) {
        char frames[640];
        int used = 0;
        for (unsigned f = 0; f < kDepth && top[i].pcs[f] != 0u && used < static_cast<int>(sizeof(frames)) - 64; ++f) {
            Dl_info info{};
            const std::uintptr_t pc = top[i].pcs[f] - 1u;  // inside the call instruction
            const char* module = "?";
            std::uintptr_t offset = pc;
            if (dladdr(reinterpret_cast<void*>(pc), &info) != 0 && info.dli_fname != nullptr) {
                module = std::strrchr(info.dli_fname, '/') != nullptr ? std::strrchr(info.dli_fname, '/') + 1
                                                                       : info.dli_fname;
                offset = pc - reinterpret_cast<std::uintptr_t>(info.dli_fbase);
            }
            used += std::snprintf(frames + used, sizeof(frames) - static_cast<std::size_t>(used), " %s+0x%lx", module,
                                  static_cast<unsigned long>(offset));
        }
        std::fprintf(stderr, "[memtrace] %2u. %.1f MiB vivos (pico %.1f) en %llu bloques, %.1f MiB pedidos en total:%s\n",
                     i + 1u, static_cast<double>(top[i].live_bytes) / 1048576.0,
                     static_cast<double>(top[i].peak_bytes) / 1048576.0,
                     static_cast<unsigned long long>(top[i].live_blocks),
                     static_cast<double>(top[i].allocated_bytes) / 1048576.0, frames);
    }
    t_inside = false;
}

}  // extern "C"
