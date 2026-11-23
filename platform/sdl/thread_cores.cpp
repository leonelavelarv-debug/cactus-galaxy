// SPDX-License-Identifier: GPL-3.0-only
// NEBULA_NUCLEOS: each of Nebula's busy threads on a core of its own, as the PS Vita port spreads its work (sdl_host.h,
// start_thread_cores). KNULLI only; off unless asked for. A watcher thread looks at /proc/self/task once a second (every
// five after the first half minute) and fixes each new thread to its core by its name:
//   vita       game "Nebula Game" on core 0, the GL thread "Nebula GL" (calls to the GPU) on 1, the GX render thread
//              "Nebula GX..." (the FIFO's translation) on 2, Nebula's other threads (host, vsync, shader builder, this
//              watcher) on 3; the rest (SDL's audio, the GPU driver's) where the system puts them.
//   vita-todo  the same, and every other thread of the process on core 3 too.
// Fewer than four cores, or a system that refuses: it says so and leaves every thread as it was.

#include "sdl_host.h"

#if defined(NEBULA_HOST_SDL2) && !defined(__ANDROID__)

#include <dirent.h>
#include <sched.h>
#include <sys/prctl.h>
#include <unistd.h>

#include <chrono>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <thread>

namespace galaxy::sdl {

namespace {

// The core for a thread of this name; -1: left to the system.
int core_for(const char* name, bool everything) {
    if (std::strncmp(name, "Nebula Game", 11) == 0) return 0;
    if (std::strcmp(name, "Nebula GL") == 0) return 1;
    if (std::strncmp(name, "Nebula GX", 9) == 0) return 2;
    if (std::strncmp(name, "Nebula", 6) == 0) return 3;
    return everything ? 3 : -1;
}

void fix_new_threads(std::set<long>& seen, bool everything) {
    DIR* tasks = opendir("/proc/self/task");
    if (tasks == nullptr) {
        return;
    }
    while (const dirent* entry = readdir(tasks)) {
        const long tid = std::strtol(entry->d_name, nullptr, 10);
        if (tid <= 0 || seen.count(tid) != 0u) {
            continue;
        }
        char path[64];
        std::snprintf(path, sizeof(path), "/proc/self/task/%ld/comm", tid);
        char name[32]{};
        if (std::FILE* file = std::fopen(path, "r")) {
            if (std::fgets(name, sizeof(name), file) == nullptr) {
                name[0] = '\0';
            }
            std::fclose(file);
        }
        name[std::strcspn(name, "\n")] = '\0';
        if (name[0] == '\0') {
            continue;  // not named yet (or gone): look again next time
        }
        seen.insert(tid);
        const int core = core_for(name, everything);
        if (core < 0) {
            continue;
        }
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(core, &set);
        if (sched_setaffinity(static_cast<pid_t>(tid), sizeof(set), &set) == 0) {
            if (std::strncmp(name, "Nebula", 6) == 0) {
                std::fprintf(stderr, "[nucleos] %s (%ld) -> core %d\n", name, tid, core);
            }
        } else {
            std::fprintf(stderr, "[nucleos] %s (%ld): %s\n", name, tid, std::strerror(errno));
        }
    }
    closedir(tasks);
}

}  // namespace

void start_thread_cores() noexcept {
    const char* mode = std::getenv("NEBULA_NUCLEOS");
    if (mode == nullptr || mode[0] == '\0' || std::strcmp(mode, "0") == 0) {
        return;
    }
    const bool everything = std::strcmp(mode, "vita-todo") == 0;
    if (!everything && std::strcmp(mode, "vita") != 0) {
        std::fprintf(stderr, "[nucleos] NEBULA_NUCLEOS=%s: unknown (vita, vita-todo); threads left as they are\n", mode);
        return;
    }
    if (sysconf(_SC_NPROCESSORS_CONF) < 4) {
        std::fprintf(stderr, "[nucleos] fewer than 4 cores; threads left as they are\n");
        return;
    }
    std::fprintf(stderr, "[nucleos] %s: game 0, GL 1, GX render 2, Nebula's others 3%s\n", mode,
                 everything ? ", every other thread 3" : "");
    std::thread([everything] {
        prctl(PR_SET_NAME, "Nebula Nucleos");
        std::set<long> seen;
        const auto start = std::chrono::steady_clock::now();
        for (;;) {
            fix_new_threads(seen, everything);
            const bool settled = std::chrono::steady_clock::now() - start > std::chrono::seconds(30);
            std::this_thread::sleep_for(std::chrono::seconds(settled ? 5 : 1));
        }
    }).detach();
}

}  // namespace galaxy::sdl

#else

namespace galaxy::sdl {
void start_thread_cores() noexcept {}
}  // namespace galaxy::sdl

#endif
