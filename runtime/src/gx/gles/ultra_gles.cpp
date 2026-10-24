// SPDX-License-Identifier: GPL-3.0-only
// ULTRA-GLES: per-frame measurement of the GLES port (ultra_gles.h).

#include "galaxy/gx/gles/ultra_gles.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <filesystem>
#include <initializer_list>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#if !defined(_WIN32)
#include <dirent.h>
#include <pthread.h>
#include <unistd.h>
#endif

extern char** environ;

namespace galaxy::gx::ultra {
namespace {

std::int64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

double env_double(const char* name, double fallback) {
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return fallback;
    }
    char* end = nullptr;
    const double parsed = std::strtod(value, &end);
    return end != value ? parsed : fallback;
}

// What made a frame late, in the order it is looked for (the first that fits is the cause).
enum Cause : unsigned { kShader = 0, kTextures, kReadback, kGpu, kRenderCpu, kOther, kCauseCount };
constexpr const char* kCauseKeys[kCauseCount] = {"shader", "texturas", "lectura_efb", "gpu", "cpu_render", "otro"};
constexpr const char* kCauseNames[kCauseCount] = {
    "un shader compilado en el hilo de render", "subida de texturas", "lectura del EFB (peek o captura)",
    "la GPU (frame en vuelo o render largo)", "CPU del hilo de render (GX, draws)", "el juego u otro hilo"};

struct Record {
    FrameSample sample;
    std::array<std::uint64_t, kCounterCount> counters{};
    double gpu_ms = -1.0;
    double interval_ms = 0.0;
    std::uint32_t scene = 0;      // index into scene names
    bool load = false;            // within the scene's first GALAXY_ULTRA_WARMUP seconds
    bool captured = false;        // its picture was read back for capturas/ (a stall of ULTRA's own: not counted)
    std::int64_t since_start_ns = 0;
};

struct Stats {
    std::uint64_t frames = 0;
    std::uint64_t drawn = 0;  // not repeats
    std::int64_t first_ns = 0, last_ns = 0;
    double interval_sum = 0.0;
    std::vector<float> intervals;
    std::vector<std::uint8_t> frame_causes;  // each interval's likely cause, should it turn out a hitch
    double cpu_ms = 0.0, draw_ms = 0.0, slot_wait_ms = 0.0, swap_wait_ms = 0.0, readback_ms = 0.0;
    double gpu_ms = 0.0, gpu_max_ms = 0.0;
    std::uint64_t gpu_frames = 0;
    std::uint64_t draws = 0, programs = 0, uber = 0, pending = 0, compiles = 0;
    double compile_ms = 0.0;
    std::uint64_t uploads = 0, upload_bytes = 0, copies = 0, peeks = 0, sync_peeks = 0, vertices = 0;
    std::uint64_t hitches = 0, stalls = 0, gl_errors = 0;
    std::uint64_t dl_compares = 0, dl_compare_bytes = 0, dl_skips = 0;
    bool gl_thread = false;
    double gl_cpu_ms = 0.0, gl_wait_ms = 0.0;
    std::uint64_t gl_syncs = 0, gl_commands = 0;
    std::array<std::uint64_t, kCauseCount> causes{};
    std::uint32_t efb_min = 0, efb_max = 0;
    double seconds() const { return last_ns > first_ns ? static_cast<double>(last_ns - first_ns) / 1e9 : 0.0; }
};

struct Memory {
    std::uint64_t samples = 0;
    double rss_sum_kib = 0.0;
    std::uint64_t rss_peak_kib = 0;
    std::uint64_t available_min_kib = ~0ull;
    // The battery (sysfs power_supply of type Battery), sampled with the memory: the draw in mW (power_now, or
    // |current_now| x voltage_now) and the charge in %, and how many samples were taken on the charger.
    std::uint64_t battery_samples = 0;
    std::uint64_t charging_samples = 0;
    double battery_mw_sum = 0.0;
    int battery_pct_first = -1;
    int battery_pct_last = -1;
    double battery_first_s = 0.0;
    double battery_last_s = 0.0;
};

struct BatteryReading {
    bool valid = false;     // a draw was read
    double mw = 0.0;
    int percent = -1;
    bool charging = false;
};

// The battery's sysfs folder: GALAXY_ULTRA_BATTERY, else the first /sys/class/power_supply/* of type Battery.
const std::string& battery_dir() {
    static const std::string dir = [] {
        if (const char* forced = std::getenv("GALAXY_ULTRA_BATTERY"); forced != nullptr && *forced != '\0') {
            return std::string(forced);
        }
#if !defined(_WIN32)
        if (DIR* list = opendir("/sys/class/power_supply")) {
            std::string found;
            while (dirent* entry = readdir(list)) {
                if (entry->d_name[0] == '.') continue;
                const std::string candidate = std::string("/sys/class/power_supply/") + entry->d_name;
                if (FILE* type = std::fopen((candidate + "/type").c_str(), "r")) {
                    char text[32] = {};
                    if (std::fgets(text, sizeof(text), type) != nullptr && std::strncmp(text, "Battery", 7) == 0) {
                        found = candidate;
                    }
                    std::fclose(type);
                }
                if (!found.empty()) break;
            }
            closedir(list);
            return found;
        }
#endif
        return std::string{};
    }();
    return dir;
}

bool read_sysfs_number(const std::string& path, long long& value) {
    FILE* file = std::fopen(path.c_str(), "r");
    if (file == nullptr) return false;
    const bool ok = std::fscanf(file, "%lld", &value) == 1;
    std::fclose(file);
    return ok;
}

BatteryReading read_battery() {
    BatteryReading out;
    const std::string& dir = battery_dir();
    if (dir.empty()) return out;
    long long value = 0;
    if (read_sysfs_number(dir + "/capacity", value)) out.percent = static_cast<int>(value);
    if (FILE* status = std::fopen((dir + "/status").c_str(), "r")) {
        char text[32] = {};
        if (std::fgets(text, sizeof(text), status) != nullptr) {
            out.charging = std::strncmp(text, "Charging", 8) == 0 || std::strncmp(text, "Full", 4) == 0;
        }
        std::fclose(status);
    }
    long long power = 0, current = 0, voltage = 0;
    const auto first = [&](std::initializer_list<const char*> names, long long& value) {
        for (const char* name : names) {
            if (read_sysfs_number(dir + "/" + name, value) && value != 0) return true;
        }
        return false;
    };
    if (first({"power_now", "power_avg"}, power)) {
        out.mw = static_cast<double>(power < 0 ? -power : power) / 1000.0;  // uW
        out.valid = true;
    } else if (first({"current_now", "current_avg"}, current) &&
               first({"voltage_now", "voltage_avg", "voltage_ocv"}, voltage) && voltage > 0) {
        // uA x uV; the sign of the current is the driver's choice (discharging is positive on some, negative on others).
        out.mw = static_cast<double>(current < 0 ? -current : current) * static_cast<double>(voltage) / 1e9;
        out.valid = true;
    }
    return out;
}

class Profiler {
public:
    Profiler() {
        const char* dir = std::getenv("GALAXY_ULTRA_DIR");
        dir_ = dir != nullptr && *dir != '\0' ? dir : "ultra";
        const char* mode = std::getenv("GALAXY_ULTRA");
        bench_ = mode != nullptr && std::strcmp(mode, "bench") == 0;
        warmup_ns_ = static_cast<std::int64_t>(env_double("GALAXY_ULTRA_WARMUP", 3.0) * 1e9);
        period_ms_ = 1000.0 / std::max(1.0, env_double("GALAXY_ULTRA_HZ", 60.0));
        // A tour asks for its pictures itself, at a game frame (request_capture); otherwise the n-th steady present.
        const bool touring = std::getenv("GALAXY_TEST_GALAXY_TOUR") != nullptr;
        capture_at_ = static_cast<std::uint64_t>(
            env_double("GALAXY_ULTRA_CAPTURE", bench_ && !touring ? 360.0 : 0.0));
        std::error_code error;
        std::filesystem::create_directories(dir_, error);
        frames_csv_ = std::fopen((dir_ + "/frames.csv").c_str(), "w");
        memory_csv_ = std::fopen((dir_ + "/memoria.csv").c_str(), "w");
        if (frames_csv_ != nullptr) {
            std::fputs("frame,t_ms,escena,carga,intervalo_ms,cpu_ms,gpu_ms,draw_ms,espera_slot_ms,espera_swap_ms,"
                       "lecturas_ms,draws,programas,uber,omitidos,compilados,compilar_ms,texturas,texturas_kib,copias,"
                       "peeks,peeks_sync,vertices,saltado,efb,errores_gl,tiron\n",
                       frames_csv_);
        }
        if (memory_csv_ != nullptr) {
            std::fputs("t_s,escena,rss_kib,disponible_kib,bateria_pct,bateria_mw,cargando\n", memory_csv_);
        }
        start_ns_ = now_ns();
        start_time_ = std::time(nullptr);
        scenes_.push_back("-");
        scene_index_.emplace("-", 0u);
        writer_ = std::thread([this] {
#if defined(__linux__)
            pthread_setname_np(pthread_self(), "Nebula Ultra");  // not taken for the render thread by the sampler
#endif
            write_loop();
        });
        std::fprintf(stderr, "[ultra] midiendo en %s (%s; hitch > %.1f ms, carga %.1f s por escena)\n", dir_.c_str(),
                     bench_ ? "benchmark" : "juego", period_ms_ * 1.5, static_cast<double>(warmup_ns_) / 1e9);
    }

    std::array<std::atomic<std::uint64_t>, kCounterCount> counters{};

    void frame(const FrameSample& sample) {
        std::array<std::uint64_t, kCounterCount> taken{};
        for (unsigned i = 0; i < kCounterCount; ++i) {
            taken[i] = counters[i].exchange(0u, std::memory_order_relaxed);
        }
        std::lock_guard<std::mutex> lock(mutex_);
        if (finished_) {
            return;
        }
        if (scene_generation_ != seen_generation_) {
            seen_generation_ = scene_generation_;
            current_scene_ = scene_for(pending_scene_);
            scene_start_ns_ = sample.present_ns;
            scene_presents_ = 0u;
        }
        if (scene_start_ns_ == 0) {
            scene_start_ns_ = sample.present_ns;
        }
        Record record{};
        record.sample = sample;
        record.counters = taken;
        record.interval_ms = last_present_ns_ != 0 ? static_cast<double>(sample.present_ns - last_present_ns_) / 1e6 : 0.0;
        last_present_ns_ = sample.present_ns;
        record.scene = current_scene_;
        record.since_start_ns = sample.present_ns - scene_start_ns_;
        record.load = record.since_start_ns < warmup_ns_;
        record.captured = std::exchange(capture_frame_, false);
        if (!record.load) {
            ++scene_presents_;
            if (capture_at_ != 0u && scene_presents_ == capture_at_) {
                request_capture(scenes_[current_scene_]);
            }
        }
        ring_.push_back(record);
        // A frame's timer queries come back kFramesInFlight frames later: commit what is older than that.
        while (ring_.size() > 6u) {
            queue_.push_back(ring_.front());
            ring_.pop_front();
        }
    }

    void gpu(std::uint64_t frame_index, double ms) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (Record& record : ring_) {
            if (record.sample.frame_index == frame_index) {
                record.gpu_ms = record.gpu_ms < 0.0 ? ms : record.gpu_ms + ms;
                return;
            }
        }
        // A frame that did not present (no record of its own): its GPU time goes to the next one that did.
        for (Record& record : ring_) {
            if (record.sample.frame_index > frame_index) {
                record.gpu_ms = record.gpu_ms < 0.0 ? ms : record.gpu_ms + ms;
                return;
            }
        }
    }

    void set_scene(const char* name) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (pending_scene_ != name) {
            pending_scene_ = name;
            ++scene_generation_;
        }
    }

    void request_capture() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!finished_) {
            const std::string& scene = scene_generation_ != seen_generation_ ? pending_scene_ : scenes_[current_scene_];
            request_capture(scene.empty() ? std::string("-") : scene);
        }
    }

    std::string capture_path() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!capture_path_.empty()) {
            capture_frame_ = true;  // the present it is written at waits for the readback
        }
        return std::exchange(capture_path_, std::string{});
    }

    void finish() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (finished_) {
                return;
            }
            finished_ = true;
            while (!ring_.empty()) {
                queue_.push_back(ring_.front());
                ring_.pop_front();
            }
            stop_ = true;
        }
        wake_.notify_all();
        if (writer_.joinable()) {
            writer_.join();
        }
        write_reports(true);
    }

private:
    std::string dir_;
    bool bench_ = false;
    std::int64_t warmup_ns_ = 0;
    double period_ms_ = 16.667;
    std::uint64_t capture_at_ = 0;
    std::int64_t start_ns_ = 0;
    std::time_t start_time_ = 0;
    FILE* frames_csv_ = nullptr;
    FILE* memory_csv_ = nullptr;

    std::mutex mutex_;  // everything below, between the render, game and writer threads
    std::condition_variable wake_;
    bool finished_ = false;
    bool stop_ = false;
    std::deque<Record> ring_;
    std::vector<Record> queue_;
    std::string pending_scene_;
    std::uint64_t scene_generation_ = 0, seen_generation_ = 0;
    std::uint32_t current_scene_ = 0;
    std::int64_t scene_start_ns_ = 0;
    std::int64_t last_present_ns_ = 0;
    std::uint64_t scene_presents_ = 0;
    std::string capture_path_;
    bool capture_frame_ = false;
    std::vector<std::string> scenes_;
    std::unordered_map<std::string, std::uint32_t> scene_index_;
    std::map<std::string, unsigned> captures_taken_;

    // The writer thread's.
    std::thread writer_;
    std::vector<Stats> scene_stats_;  // by scene index, steady frames
    Stats total_, load_;
    std::uint64_t captures_ = 0;
    std::vector<Stats> scene_load_;
    Memory memory_;

    std::uint32_t scene_for(const std::string& name) {
        const std::string key = name.empty() ? std::string("-") : name;
        const auto [it, added] = scene_index_.emplace(key, static_cast<std::uint32_t>(scenes_.size()));
        if (added) {
            scenes_.push_back(key);
        }
        return it->second;
    }

    void request_capture(const std::string& scene) {
        std::error_code error;
        std::filesystem::create_directories(dir_ + "/capturas", error);
        const unsigned visit = ++captures_taken_[scene];
        std::string name = scene;
        std::replace_if(name.begin(), name.end(), [](char c) { return c == '/' || c == '\\' || c == ' '; }, '_');
        capture_path_ = dir_ + "/capturas/" + name + (visit > 1u ? "-" + std::to_string(visit) : "") + ".ppm";
    }

    std::array<double, 2> recent_gpu_{-1.0, -1.0};  // the writer's: the two frames before the one it looks at

    Cause cause_of(const Record& r) const {
        const double cpu_ms = static_cast<double>(r.sample.cpu_ns) / 1e6;
        if (r.counters[kSyncCompiles] != 0u) {
            return kShader;
        }
        if (r.counters[kTextureUploadBytes] > 512u * 1024u) {
            return kTextures;
        }
        if (r.sample.sync_peeks != 0u || r.sample.readback_ns > 4'000'000) {
            return kReadback;
        }
        // A long GPU frame shows up a frame or two later, when its slot comes round again.
        const double recent_gpu = std::max({r.gpu_ms, recent_gpu_[0], recent_gpu_[1]});
        if (r.sample.slot_wait_ns > 6'000'000 || recent_gpu > period_ms_) {
            return kGpu;
        }
        if (cpu_ms > period_ms_) {
            return kRenderCpu;
        }
        return kOther;
    }

    // slow: over 1.5 periods (a missed vsync); hitches are decided at the end, against the scene's own median.
    static void account(Stats& s, const Record& r, bool slow, Cause cause, bool stall) {
        const FrameSample& f = r.sample;
        if (s.frames == 0u) {
            s.first_ns = f.present_ns;
            s.efb_min = s.efb_max = f.efb_width;
        }
        s.last_ns = f.present_ns;
        ++s.frames;
        if (f.repeats == 0u) {
            ++s.drawn;
        }
        if (r.interval_ms > 0.0) {
            s.interval_sum += r.interval_ms;
            s.intervals.push_back(static_cast<float>(r.interval_ms));
            s.frame_causes.push_back(static_cast<std::uint8_t>(cause));
        }
        s.cpu_ms += static_cast<double>(f.cpu_ns) / 1e6;
        s.draw_ms += static_cast<double>(f.draw_ns) / 1e6;
        s.slot_wait_ms += static_cast<double>(f.slot_wait_ns) / 1e6;
        s.swap_wait_ms += static_cast<double>(f.swap_wait_ns) / 1e6;
        s.readback_ms += static_cast<double>(f.readback_ns + f.sync_peek_ns) / 1e6;
        if (r.gpu_ms >= 0.0) {
            s.gpu_ms += r.gpu_ms;
            s.gpu_max_ms = std::max(s.gpu_max_ms, r.gpu_ms);
            ++s.gpu_frames;
        }
        s.draws += f.draws;
        s.programs += f.program_changes;
        s.uber += r.counters[kUberDraws];
        s.pending += r.counters[kPendingSkips] + f.skipped_draws;
        s.compiles += r.counters[kSyncCompiles];
        s.compile_ms += static_cast<double>(r.counters[kSyncCompileNs]) / 1e6;
        s.uploads += r.counters[kTextureUploads];
        s.upload_bytes += r.counters[kTextureUploadBytes];
        s.copies += f.copies;
        s.peeks += f.peeks;
        s.sync_peeks += f.sync_peeks;
        s.vertices += f.vertices;
        s.gl_errors += f.gl_errors;
        s.dl_compares += r.counters[kDisplayListCompares];
        s.dl_compare_bytes += r.counters[kDisplayListCompareBytes];
        s.dl_skips += r.counters[kDisplayListCompareSkips];
        if (f.gl_thread) {
            s.gl_thread = true;
            s.gl_cpu_ms += static_cast<double>(f.gl_cpu_ns) / 1e6;
            s.gl_wait_ms += static_cast<double>(f.gl_sync_wait_ns) / 1e6;
            s.gl_syncs += f.gl_syncs;
            s.gl_commands += f.gl_commands;
        }
        s.efb_min = std::min(s.efb_min, f.efb_width);
        s.efb_max = std::max(s.efb_max, f.efb_width);
        if (slow) {
            ++s.hitches;  // the slow frames (the hitches proper are counted in figures())
        }
        if (stall) {
            ++s.stalls;
        }
    }

    void write_loop() {
        std::int64_t next_memory = now_ns();
        std::int64_t next_report = next_memory + 20'000'000'000;
        for (;;) {
            std::vector<Record> batch;
            bool stop = false;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                wake_.wait_for(lock, std::chrono::seconds(1), [this] { return stop_; });
                batch.swap(queue_);
                stop = stop_;
            }
            for (const Record& r : batch) {
                consume(r);
            }
            const std::int64_t now = now_ns();
            if (now >= next_memory || stop) {
                sample_memory(now);
                next_memory = now + 1'000'000'000;
            }
            if (frames_csv_ != nullptr) {
                std::fflush(frames_csv_);
            }
            if (memory_csv_ != nullptr) {
                std::fflush(memory_csv_);
            }
            if (stop) {
                return;
            }
            // The reports so far, every 20 s: a run that is stopped (or killed) still leaves them.
            if (now >= next_report) {
                write_reports(false);
                next_report = now + 20'000'000'000;
            }
        }
    }

    std::string scene_name(std::uint32_t index) {
        std::lock_guard<std::mutex> lock(mutex_);
        return index < scenes_.size() ? scenes_[index] : std::string("-");
    }

    void consume(const Record& r) {
        const bool hitch = r.interval_ms > period_ms_ * 1.5;
        const bool stall = r.interval_ms > 250.0;
        const Cause cause = cause_of(r);
        recent_gpu_ = {r.gpu_ms, recent_gpu_[0]};
        if (scene_stats_.size() <= r.scene) {
            scene_stats_.resize(r.scene + 1u);
            scene_load_.resize(r.scene + 1u);
        }
        if (r.captured) {
            ++captures_;
        } else if (r.load) {
            account(load_, r, hitch, cause, stall);
            account(scene_load_[r.scene], r, hitch, cause, stall);
        } else {
            account(total_, r, hitch, cause, stall);
            account(scene_stats_[r.scene], r, hitch, cause, stall);
        }
        if (frames_csv_ != nullptr) {
            const FrameSample& f = r.sample;
            const std::string scene = scene_name(r.scene);
            std::fprintf(frames_csv_,
                         "%llu,%.3f,%s,%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%u,%u,%llu,%llu,%llu,%.3f,%llu,%llu,%u,%u,%u,"
                         "%u,%d,%u,%u,%s\n",
                         static_cast<unsigned long long>(f.frame_index),
                         static_cast<double>(f.present_ns - start_ns_) / 1e6, scene.c_str(), r.load ? 1 : 0,
                         r.interval_ms, static_cast<double>(f.cpu_ns) / 1e6, r.gpu_ms,
                         static_cast<double>(f.draw_ns) / 1e6, static_cast<double>(f.slot_wait_ns) / 1e6,
                         static_cast<double>(f.swap_wait_ns) / 1e6,
                         static_cast<double>(f.readback_ns + f.sync_peek_ns) / 1e6, f.draws, f.program_changes,
                         static_cast<unsigned long long>(r.counters[kUberDraws]),
                         static_cast<unsigned long long>(r.counters[kPendingSkips] + f.skipped_draws),
                         static_cast<unsigned long long>(r.counters[kSyncCompiles]),
                         static_cast<double>(r.counters[kSyncCompileNs]) / 1e6,
                         static_cast<unsigned long long>(r.counters[kTextureUploads]),
                         static_cast<unsigned long long>(r.counters[kTextureUploadBytes] >> 10u), f.copies, f.peeks,
                         f.sync_peeks, f.vertices, f.repeats != 0u ? 1 : 0, f.efb_width, f.gl_errors,
                         r.captured ? "captura" : hitch ? kCauseKeys[cause] : "");
        }
    }

    void sample_memory(std::int64_t now) {
        std::uint64_t rss_kib = 0, available_kib = 0;
#if !defined(_WIN32)
        if (FILE* statm = std::fopen("/proc/self/statm", "r")) {
            unsigned long long size = 0, resident = 0;
            if (std::fscanf(statm, "%llu %llu", &size, &resident) == 2) {
                rss_kib = resident * static_cast<unsigned long long>(sysconf(_SC_PAGESIZE)) / 1024u;
            }
            std::fclose(statm);
        }
        if (FILE* meminfo = std::fopen("/proc/meminfo", "r")) {
            char line[128];
            while (std::fgets(line, sizeof(line), meminfo) != nullptr) {
                unsigned long long value = 0;
                if (std::sscanf(line, "MemAvailable: %llu kB", &value) == 1) {
                    available_kib = value;
                    break;
                }
            }
            std::fclose(meminfo);
        }
#endif
        if (rss_kib == 0u) {
            return;
        }
        const BatteryReading battery = read_battery();
        const double t_s = static_cast<double>(now - start_ns_) / 1e9;
        if (battery.valid) {
            ++memory_.battery_samples;
            memory_.battery_mw_sum += battery.mw;
            memory_.charging_samples += battery.charging ? 1u : 0u;
        }
        if (battery.percent >= 0) {
            if (memory_.battery_pct_first < 0) {
                memory_.battery_pct_first = battery.percent;
                memory_.battery_first_s = t_s;
            }
            memory_.battery_pct_last = battery.percent;
            memory_.battery_last_s = t_s;
        }
        ++memory_.samples;
        memory_.rss_sum_kib += static_cast<double>(rss_kib);
        memory_.rss_peak_kib = std::max(memory_.rss_peak_kib, rss_kib);
        if (available_kib != 0u) {
            memory_.available_min_kib = std::min(memory_.available_min_kib, available_kib);
        }
        if (memory_csv_ != nullptr) {
            std::uint32_t scene = 0;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                scene = current_scene_;
            }
            std::fprintf(memory_csv_, "%.1f,%s,%llu,%llu,%d,%.0f,%d\n", t_s, scene_name(scene).c_str(),
                         static_cast<unsigned long long>(rss_kib), static_cast<unsigned long long>(available_kib),
                         battery.percent, battery.valid ? battery.mw : -1.0, battery.charging ? 1 : 0);
        }
    }

    struct Figures {
        double seconds = 0, fps = 0, drawn_fps = 0, mean = 0, p50 = 0, p95 = 0, p99 = 0, max = 0, low1 = 0;
        double hitches_per_min = 0, cpu = 0, gpu = 0, draws = 0, programs = 0, uber_pct = 0, upload_kib_s = 0;
        // A hitch: a frame over twice the scene's median (and over 1.5 periods), so a scene that runs slowly
        // throughout is not all hitches; its causes; the slow frames (over 1.5 periods) apart.
        double hitch_ms = 0;
        std::uint64_t hitches = 0, slow = 0;
        std::array<std::uint64_t, kCauseCount> causes{};
    };

    Figures figures(Stats s) const {
        Figures f{};
        f.slow = s.hitches;
        if (s.frames == 0u) {
            return f;
        }
        // The time the frames themselves took (their intervals): other scenes and loads in between do not count.
        const double active_s = s.interval_sum / 1000.0;
        f.seconds = active_s;
        if (active_s > 0.0) {
            f.fps = static_cast<double>(s.intervals.size()) / active_s;
            f.drawn_fps = static_cast<double>(s.drawn) / active_s;
            f.upload_kib_s = static_cast<double>(s.upload_bytes) / 1024.0 / active_s;
        }
        const std::vector<float> in_order = s.intervals;
        std::vector<float>& v = s.intervals;
        if (!v.empty()) {
            std::sort(v.begin(), v.end());
            const auto at = [&](double q) {
                const std::size_t i = std::min(v.size() - 1u, static_cast<std::size_t>(q * static_cast<double>(v.size())));
                return static_cast<double>(v[i]);
            };
            f.mean = s.interval_sum / static_cast<double>(v.size());
            f.p50 = at(0.50);
            f.p95 = at(0.95);
            f.p99 = at(0.99);
            f.max = v.back();
            const std::size_t worst = std::max<std::size_t>(1u, v.size() / 100u);
            double sum = 0.0;
            for (std::size_t i = v.size() - worst; i < v.size(); ++i) {
                sum += v[i];
            }
            f.low1 = sum > 0.0 ? 1000.0 * static_cast<double>(worst) / sum : 0.0;
            f.hitch_ms = std::max(period_ms_ * 1.5, f.p50 * 2.0);
        }
        for (std::size_t i = 0; i < in_order.size() && i < s.frame_causes.size(); ++i) {
            if (in_order[i] > f.hitch_ms) {
                ++f.hitches;
                ++f.causes[s.frame_causes[i]];
            }
        }
        if (f.seconds > 0.0) {
            f.hitches_per_min = static_cast<double>(f.hitches) * 60.0 / f.seconds;
        }
        const double frames = static_cast<double>(s.frames);
        f.cpu = s.cpu_ms / frames;
        f.gpu = s.gpu_frames != 0u ? s.gpu_ms / static_cast<double>(s.gpu_frames) : -1.0;
        f.draws = static_cast<double>(s.draws) / frames;
        f.programs = static_cast<double>(s.programs) / frames;
        const double all = static_cast<double>(s.draws);
        f.uber_pct = all > 0.0 ? 100.0 * static_cast<double>(s.uber) / all : 0.0;
        return f;
    }

    static std::string json_string(const std::string& text) {
        std::string out = "\"";
        for (const char c : text) {
            if (c == '"' || c == '\\') {
                out += '\\';
                out += c;
            } else if (static_cast<unsigned char>(c) < 0x20u) {
                char escaped[8];
                std::snprintf(escaped, sizeof(escaped), "\\u%04x", static_cast<unsigned>(c));
                out += escaped;
            } else {
                out += c;
            }
        }
        return out + "\"";
    }

    // The total's hitches are each scene's against its own median, added up.
    Figures total_figures() const {
        Figures t = figures(total_);
        t.hitches = 0;
        t.causes = {};
        for (const Stats& scene : scene_stats_) {
            const Figures f = figures(scene);
            t.hitches += f.hitches;
            for (unsigned c = 0; c < kCauseCount; ++c) {
                t.causes[c] += f.causes[c];
            }
        }
        t.hitches_per_min = t.seconds > 0.0 ? static_cast<double>(t.hitches) * 60.0 / t.seconds : 0.0;
        return t;
    }

    std::string json_stats(const Stats& s, const Figures* given = nullptr) const {
        const Figures f = given != nullptr ? *given : figures(s);
        char text[2048];
        std::snprintf(text, sizeof(text),
                      "{\"frames\": %llu, \"segundos\": %.2f, \"fps\": %.2f, \"fps_dibujados\": %.2f, "
                      "\"frame_ms\": {\"media\": %.3f, \"p50\": %.3f, \"p95\": %.3f, \"p99\": %.3f, \"max\": %.3f}, "
                      "\"fps_1pct_bajo\": %.2f, \"tirones\": %llu, \"tirones_min\": %.2f, \"umbral_tiron_ms\": %.2f, "
                      "\"frames_lentos\": %llu, \"paradas\": %llu, "
                      "\"cpu_render_ms\": %.3f, \"gpu_ms\": %.3f, \"gpu_max_ms\": %.3f, \"draws\": %.1f, "
                      "\"cambios_programa\": %.1f, \"uber_pct\": %.2f, \"omitidos\": %llu, \"compilados\": %llu, "
                      "\"compilar_ms\": %.1f, \"texturas_kib_s\": %.1f, \"copias\": %.2f, \"peeks\": %.2f, "
                      "\"peeks_sync\": %llu, \"vertices\": %.0f, \"errores_gl\": %llu, \"efb\": [%u, %u], "
                      "\"dl_comparadas\": %.1f, \"dl_comparadas_kib\": %.1f, \"dl_saltadas\": %.1f, "
                      "\"hilo_gl\": %s, \"gl_cpu_ms\": %.3f, \"gl_espera_ms\": %.3f, \"gl_esperas\": %.2f, "
                      "\"gl_llamadas\": %.0f, "
                      "\"causas\": {",
                      static_cast<unsigned long long>(s.frames), f.seconds, f.fps, f.drawn_fps, f.mean, f.p50, f.p95,
                      f.p99, f.max, f.low1, static_cast<unsigned long long>(f.hitches), f.hitches_per_min, f.hitch_ms,
                      static_cast<unsigned long long>(f.slow), static_cast<unsigned long long>(s.stalls), f.cpu, f.gpu, s.gpu_max_ms, f.draws, f.programs,
                      f.uber_pct, static_cast<unsigned long long>(s.pending),
                      static_cast<unsigned long long>(s.compiles), s.compile_ms, f.upload_kib_s,
                      s.frames != 0u ? static_cast<double>(s.copies) / static_cast<double>(s.frames) : 0.0,
                      s.frames != 0u ? static_cast<double>(s.peeks) / static_cast<double>(s.frames) : 0.0,
                      static_cast<unsigned long long>(s.sync_peeks),
                      s.frames != 0u ? static_cast<double>(s.vertices) / static_cast<double>(s.frames) : 0.0,
                      static_cast<unsigned long long>(s.gl_errors), s.efb_min, s.efb_max,
                      s.frames != 0u ? static_cast<double>(s.dl_compares) / static_cast<double>(s.frames) : 0.0,
                      s.frames != 0u ? static_cast<double>(s.dl_compare_bytes) / 1024.0 / static_cast<double>(s.frames) : 0.0,
                      s.frames != 0u ? static_cast<double>(s.dl_skips) / static_cast<double>(s.frames) : 0.0,
                      s.gl_thread ? "true" : "false",
                      s.gl_thread && s.frames != 0u ? s.gl_cpu_ms / static_cast<double>(s.frames) : -1.0,
                      s.gl_thread && s.frames != 0u ? s.gl_wait_ms / static_cast<double>(s.frames) : -1.0,
                      s.frames != 0u ? static_cast<double>(s.gl_syncs) / static_cast<double>(s.frames) : 0.0,
                      s.frames != 0u ? static_cast<double>(s.gl_commands) / static_cast<double>(s.frames) : 0.0);
        std::string out = text;
        for (unsigned c = 0; c < kCauseCount; ++c) {
            out += (c != 0u ? ", " : "") + json_string(kCauseKeys[c]) + ": " + std::to_string(f.causes[c]);
        }
        return out + "}}";
    }

    std::vector<std::pair<std::string, std::string>> settings() const {
        std::vector<std::pair<std::string, std::string>> out;
        for (char** entry = environ; entry != nullptr && *entry != nullptr; ++entry) {
            const std::string text = *entry;
            const std::size_t equals = text.find('=');
            if (equals == std::string::npos) {
                continue;
            }
            const std::string name = text.substr(0, equals);
            if ((name.rfind("GALAXY_", 0) == 0 || name.rfind("NEBULA_", 0) == 0) && name != "GALAXY_ULTRA_DIR") {
                out.emplace_back(name, text.substr(equals + 1));
            }
        }
        std::sort(out.begin(), out.end());
        return out;
    }

    // final: the run's end (closes the CSVs); otherwise the figures so far, marked partial. Each file is written to a
    // temporary name and renamed, so a run killed halfway never leaves half a report.
    void write_reports(bool final) {
        char started[32];
        std::strftime(started, sizeof(started), "%Y-%m-%dT%H:%M:%S", std::localtime(&start_time_));
        const double run_s = static_cast<double>(now_ns() - start_ns_) / 1e9;
        const auto config = settings();
        const std::string json_path = dir_ + "/resumen.json";
        if (FILE* json = std::fopen((json_path + ".tmp").c_str(), "w")) {
            std::fprintf(json, "{\n  \"ultra\": 1,\n  \"inicio\": \"%s\",\n  \"duracion_s\": %.1f,\n  \"modo\": \"%s\",\n"
                               "  \"parcial\": %s,\n",
                         started, run_s, bench_ ? "bench" : "juego", final ? "false" : "true");
            std::fprintf(json, "  \"periodo_ms\": %.3f,\n  \"ajustes\": {", period_ms_);
            for (std::size_t i = 0; i < config.size(); ++i) {
                std::fprintf(json, "%s%s: %s", i != 0u ? ", " : "", json_string(config[i].first).c_str(),
                             json_string(config[i].second).c_str());
            }
            std::fprintf(json, "},\n  \"memoria\": {\"rss_media_kib\": %.0f, \"rss_pico_kib\": %llu, "
                               "\"disponible_min_kib\": %llu},\n",
                         memory_.samples != 0u ? memory_.rss_sum_kib / static_cast<double>(memory_.samples) : 0.0,
                         static_cast<unsigned long long>(memory_.rss_peak_kib),
                         static_cast<unsigned long long>(memory_.available_min_kib == ~0ull ? 0u
                                                                                            : memory_.available_min_kib));
            std::fprintf(json, "  \"bateria\": {\"mw_media\": %.0f, \"muestras\": %llu, \"cargando_muestras\": %llu, "
                               "\"pct_inicio\": %d, \"pct_fin\": %d, \"minutos\": %.1f, \"carpeta\": %s},\n",
                         memory_.battery_samples != 0u ? memory_.battery_mw_sum / static_cast<double>(memory_.battery_samples)
                                                       : -1.0,
                         static_cast<unsigned long long>(memory_.battery_samples),
                         static_cast<unsigned long long>(memory_.charging_samples), memory_.battery_pct_first,
                         memory_.battery_pct_last, (memory_.battery_last_s - memory_.battery_first_s) / 60.0,
                         json_string(battery_dir()).c_str());
            const Figures total = total_figures();
            std::fprintf(json, "  \"total\": %s,\n  \"carga\": %s,\n  \"escenas\": [", json_stats(total_, &total).c_str(),
                         json_stats(load_).c_str());
            bool first = true;
            for (std::size_t i = 0; i < scene_stats_.size(); ++i) {
                if (scene_stats_[i].frames == 0u && scene_load_[i].frames == 0u) {
                    continue;
                }
                std::fprintf(json, "%s\n    {\"nombre\": %s, \"estable\": %s, \"carga\": %s}", first ? "" : ",",
                             json_string(scene_name(static_cast<std::uint32_t>(i))).c_str(),
                             json_stats(scene_stats_[i]).c_str(), json_stats(scene_load_[i]).c_str());
                first = false;
            }
            std::fprintf(json, "\n  ]\n}\n");
            std::fclose(json);
            std::error_code error;
            std::filesystem::rename(json_path + ".tmp", json_path, error);
        }
        write_text_report(started, run_s, config, final);
        if (!final) {
            return;
        }
        if (frames_csv_ != nullptr) {
            std::fclose(frames_csv_);
            frames_csv_ = nullptr;
        }
        if (memory_csv_ != nullptr) {
            std::fclose(memory_csv_);
            memory_csv_ = nullptr;
        }
        const Figures t = total_figures();
        std::fprintf(stderr,
                     "[ultra] %.1f FPS (%.1f dibujados), frame %.2f ms (p95 %.2f, p99 %.2f), 1%% bajo %.1f FPS, "
                     "%llu tirones; informe en %s/informe.txt\n",
                     t.fps, t.drawn_fps, t.mean, t.p95, t.p99, t.low1, static_cast<unsigned long long>(t.hitches),
                     dir_.c_str());
    }

    // The analyser: what costs the most in this run and which setting (or which ULTRA-GLES optimiser) to try first.
    void write_diagnosis(FILE* out, const Figures& t, double compile_per, double draw_per, double rest_per) const {
        struct Finding {
            double weight;  // ms per frame it costs, roughly
            std::string text;
        };
        std::vector<Finding> findings;
        const double per = total_.frames != 0u ? 1.0 / static_cast<double>(total_.frames) : 0.0;
        char line[512];
        if (compile_per > 0.5) {
            std::snprintf(line, sizeof(line),
                          "Shaders compilados en el hilo de render: %.1f ms por frame (%.0f s en total). La cache "
                          "(data/shadercache) los guarda: una segunda vuelta mide sin ellos. Para no esperarlos: "
                          "GALAXY_GLES_PENDING=uber (dibuja con el uber mientras compila) o skip.",
                          compile_per, (total_.compile_ms + load_.compile_ms) / 1000.0);
            findings.push_back({compile_per, line});
        }
        if (rest_per > 4.0) {
            std::snprintf(line, sizeof(line),
                          "El trabajo GX del hilo de render fuera de los draws: %.1f ms por frame (leer el FIFO, "
                          "convertir %.0f vertices por frame, decodificar texturas, copias del EFB). Para ver en que "
                          "funciones: NEBULA_ULTRA_HILOS=1.",
                          rest_per, total_.frames != 0u ? static_cast<double>(total_.vertices) * per : 0.0);
            findings.push_back({rest_per, line});
        }
        if (draw_per > 3.0) {
            std::snprintf(line, sizeof(line),
                          "Los draws: %.1f ms por frame, %.0f draws (%.0f us cada uno, sobre todo el driver).",
                          draw_per, t.draws, t.draws > 0.0 ? draw_per * 1000.0 / t.draws : 0.0);
            findings.push_back({draw_per, line});
        }
        const double gpu_drawn = t.gpu >= 0.0 && t.drawn_fps > 0.0 && t.fps > 0.0 ? t.gpu * t.fps / t.drawn_fps : -1.0;
        if (gpu_drawn > period_ms_ * 0.6 || total_.slot_wait_ms * per > 2.0) {
            std::snprintf(line, sizeof(line),
                          "La GPU: %.1f ms por frame dibujado y %.1f ms de espera por frame, con el EFB a %u px de "
                          "ancho. Prueba GALAXY_GLES_RESOLUTION=%s.",
                          gpu_drawn, total_.slot_wait_ms * per, total_.efb_max,
                          total_.efb_max >= 640u ? "75 (o auto)" : total_.efb_max > 400u ? "50 (o auto)" : "auto");
            findings.push_back({std::max(gpu_drawn - period_ms_ * 0.5, total_.slot_wait_ms * per), line});
        }
        if (total_.readback_ms * per > 1.0) {
            std::snprintf(line, sizeof(line),
                          "Lecturas del EFB (destellos del sol, puntero): %.1f ms por frame. GALAXY_PEEK_MODE=off "
                          "mide cuanto cuestan (sin ellas el sol no se oculta tras los objetos).",
                          total_.readback_ms * per);
            findings.push_back({total_.readback_ms * per, line});
        }
        if (memory_.available_min_kib != ~0ull && memory_.available_min_kib < 100u * 1024u) {
            findings.push_back({1.0, "Memoria justa: menos de 100 MiB libres en algun momento. Baja GALAXY_TEXTURE_POOL_MB "
                                     "o GALAXY_GX_DISPLAY_LIST_CACHE_MB."});
        }
        std::sort(findings.begin(), findings.end(), [](const Finding& a, const Finding& b) { return a.weight > b.weight; });
        std::fprintf(out, "Lo que mas pesa (de mas a menos) y que probar:\n");
        if (findings.empty()) {
            std::fprintf(out, "  Nada destaca: el frame cabe en su tiempo.\n");
        }
        for (std::size_t i = 0; i < findings.size(); ++i) {
            std::fprintf(out, "  %zu. %s\n", i + 1u, findings[i].text.c_str());
        }
        std::fprintf(out, "\n");
    }

    void write_text_report(const char* started, double run_s,
                           const std::vector<std::pair<std::string, std::string>>& config, bool final) {
        const std::string path = dir_ + "/informe.txt";
        FILE* out = std::fopen((path + ".tmp").c_str(), "w");
        if (out == nullptr) {
            return;
        }
        const Figures t = total_figures();
        std::fprintf(out, "ULTRA-GLES -- informe de rendimiento (%s, %s, %.0f s%s)\n\n", started,
                     bench_ ? "benchmark" : "partida", run_s,
                     final ? "" : "; PARCIAL: la ejecucion seguia, se actualiza cada 20 s");
        std::fprintf(out, "Juego estable (sin los primeros %.0f s de cada escena):\n", static_cast<double>(warmup_ns_) / 1e9);
        std::fprintf(out, "  FPS promedio ............ %.1f mostrados, %.1f dibujados (el resto los repite el salto de frames)\n",
                     t.fps, t.drawn_fps);
        std::fprintf(out, "  Tiempo por fotograma .... media %.2f ms, p50 %.2f, p95 %.2f, p99 %.2f, maximo %.1f\n", t.mean,
                     t.p50, t.p95, t.p99, t.max);
        std::fprintf(out, "  1%% mas lento ............ %.1f FPS\n", t.low1);
        std::fprintf(out, "  Tirones ................. %llu (%.1f por minuto): frames de mas del doble de la mediana de su escena\n",
                     static_cast<unsigned long long>(t.hitches), t.hitches_per_min);
        if (t.hitches != 0u) {
            std::fprintf(out, "    causas probables:\n");
            for (unsigned c = 0; c < kCauseCount; ++c) {
                if (t.causes[c] != 0u) {
                    std::fprintf(out, "      %5llu  %s\n", static_cast<unsigned long long>(t.causes[c]), kCauseNames[c]);
                }
            }
        }
        std::fprintf(out, "  Frames lentos ........... %llu de %llu pasan de %.0f ms (se pierde un vsync); paradas de mas de 250 ms: %llu\n",
                     static_cast<unsigned long long>(t.slow), static_cast<unsigned long long>(total_.frames),
                     period_ms_ * 1.5, static_cast<unsigned long long>(total_.stalls));
        const double per = total_.frames != 0u ? 1.0 / static_cast<double>(total_.frames) : 0.0;
        const double compile_per = total_.compile_ms * per;
        const double draw_per = total_.draw_ms * per;
        const double rest_per = std::max(0.0, t.cpu - compile_per - draw_per);
        std::fprintf(out, "  CPU del hilo de render .. %.2f ms por frame: compilar shaders %.2f, draws %.2f, el resto %.2f (GX: FIFO,\n"
                          "                            vertices, texturas, copias); y espera %.2f a la GPU, %.2f al vsync, %.2f a lecturas\n",
                     t.cpu, compile_per, draw_per, rest_per, total_.slot_wait_ms * per, total_.swap_wait_ms * per,
                     total_.readback_ms * per);
        if (t.gpu >= 0.0) {
            std::fprintf(out, "  GPU ..................... %.2f ms por frame (maximo %.1f)\n", t.gpu, total_.gpu_max_ms);
        } else {
            std::fprintf(out, "  GPU ..................... sin temporizadores (el driver no da GL_EXT_disjoint_timer_query)\n");
        }
        std::fprintf(out, "  Draws ................... %.0f por frame, %.0f cambios de programa, %.1f %% con shader uber\n",
                     t.draws, t.programs, t.uber_pct);
        std::fprintf(out, "  Shaders en el hilo ...... %llu compilados mientras un draw esperaba (%.0f ms en total)\n",
                     static_cast<unsigned long long>(total_.compiles + load_.compiles),
                     total_.compile_ms + load_.compile_ms);
        std::fprintf(out, "  Texturas ................ %.0f KiB/s subidas\n", t.upload_kib_s);
        if (total_.gl_thread) {
            std::fprintf(out, "  Hilo GL ................. %.2f ms de CPU por frame en el driver (%.0f llamadas de GL); el hilo de render\n"
                              "                            le espero %.2f ms (%.2f llamadas por frame que necesitaban respuesta)\n",
                         total_.gl_cpu_ms * per, static_cast<double>(total_.gl_commands) * per, total_.gl_wait_ms * per,
                         static_cast<double>(total_.gl_syncs) * per);
        }
        if (total_.dl_compares + total_.dl_skips != 0u) {
            std::fprintf(out, "  Display lists ........... %.1f comparadas con la memoria por frame (%.1f KiB), %.1f sin comparar (ya\n"
                              "                            comparadas en ese frame)\n",
                         static_cast<double>(total_.dl_compares) * per,
                         static_cast<double>(total_.dl_compare_bytes) / 1024.0 * per,
                         static_cast<double>(total_.dl_skips) * per);
        }
        std::fprintf(out, "  Memoria ................. %.0f MiB de media, pico %.0f MiB; minimo libre en el sistema %.0f MiB\n",
                     memory_.samples != 0u ? memory_.rss_sum_kib / static_cast<double>(memory_.samples) / 1024.0 : 0.0,
                     static_cast<double>(memory_.rss_peak_kib) / 1024.0,
                     memory_.available_min_kib == ~0ull ? 0.0 : static_cast<double>(memory_.available_min_kib) / 1024.0);
        if (memory_.battery_samples != 0u) {
            std::fprintf(out, "  Bateria ................. %.0f mW de media; %d %% -> %d %% en %.1f min%s\n",
                         memory_.battery_mw_sum / static_cast<double>(memory_.battery_samples), memory_.battery_pct_first,
                         memory_.battery_pct_last, (memory_.battery_last_s - memory_.battery_first_s) / 60.0,
                         memory_.charging_samples != 0u ? " (CARGANDO en parte: el consumo no vale para comparar)" : "");
        } else {
            std::fprintf(out, "  Bateria ................. sin lectura (%s)\n",
                         battery_dir().empty() ? "no hay power_supply de tipo Battery" : battery_dir().c_str());
        }
        std::fprintf(out, "  Calidad ................. EFB %u-%u px de ancho (640 = la Wii)\n", total_.efb_min,
                     total_.efb_max);
        std::fprintf(out, "  Estabilidad ............. %llu errores de GL\n\n",
                     static_cast<unsigned long long>(total_.gl_errors + load_.gl_errors));
        const Figures l = figures(load_);
        std::fprintf(out, "Cargas (primeros segundos de cada escena): frame maximo %.0f ms, %llu compilaciones (%.0f ms)\n\n",
                     l.max, static_cast<unsigned long long>(load_.compiles), load_.compile_ms);
        write_diagnosis(out, t, compile_per, draw_per, rest_per);

        std::fprintf(out, "%-28s %6s %6s %7s %7s %7s %6s %6s %6s %6s\n", "Escena", "FPS", "dibuj", "p95 ms",
                     "p99 ms", "1%bajo", "tiron", "CPU", "GPU", "draws");
        for (std::size_t i = 0; i < scene_stats_.size(); ++i) {
            const Stats& s = scene_stats_[i];
            if (s.frames < 30u) {
                continue;
            }
            const Figures f = figures(s);
            std::fprintf(out, "%-28.28s %6.1f %6.1f %7.2f %7.2f %7.1f %6llu %6.2f %6.2f %6.0f\n",
                         scene_name(static_cast<std::uint32_t>(i)).c_str(), f.fps, f.drawn_fps, f.p95, f.p99, f.low1,
                         static_cast<unsigned long long>(f.hitches), f.cpu, f.gpu, f.draws);
        }
        std::fprintf(out, "\nAjustes de esta ejecucion:\n");
        for (const auto& [name, value] : config) {
            std::fprintf(out, "  %s=%s\n", name.c_str(), value.c_str());
        }
        std::fprintf(out, "\nDatos por frame: frames.csv; memoria por segundo: memoria.csv; para comparar dos "
                          "ejecuciones: python3 smg/ultra/compare.py <carpeta A> <carpeta B>\n");
        std::fclose(out);
        std::error_code error;
        std::filesystem::rename(path + ".tmp", path, error);
    }
};

std::atomic<int> g_state{-1};  // -1 unknown, 0 off, 1 on
std::mutex g_create_mutex;
Profiler* g_profiler = nullptr;

Profiler* profiler() noexcept {
    int state = g_state.load(std::memory_order_acquire);
    if (state < 0) {
        std::lock_guard<std::mutex> lock(g_create_mutex);
        state = g_state.load(std::memory_order_relaxed);
        if (state < 0) {
            const char* value = std::getenv("GALAXY_ULTRA");
            state = value != nullptr && *value != '\0' && std::strcmp(value, "0") != 0 ? 1 : 0;
            if (state == 1) {
                try {
                    g_profiler = new Profiler();
                    std::atexit([] { finish(); });
                } catch (...) {
                    g_profiler = nullptr;
                    state = 0;
                }
            }
            g_state.store(state, std::memory_order_release);
        }
    }
    return state == 1 ? g_profiler : nullptr;
}

}  // namespace

bool enabled() noexcept {
    return profiler() != nullptr;
}

void add(Counter counter, std::uint64_t value) noexcept {
    if (Profiler* p = profiler()) {
        p->counters[counter].fetch_add(value, std::memory_order_relaxed);
    }
}

void frame(const FrameSample& sample) noexcept {
    if (Profiler* p = profiler()) {
        p->frame(sample);
    }
}

void gpu(std::uint64_t frame_index, double total_ms) noexcept {
    if (Profiler* p = profiler()) {
        p->gpu(frame_index, total_ms);
    }
}

void set_scene(const char* name) noexcept {
    if (name == nullptr) {
        return;
    }
    if (Profiler* p = profiler()) {
        p->set_scene(name);
    }
}

void request_capture() noexcept {
    if (Profiler* p = profiler()) {
        try {
            p->request_capture();
        } catch (...) {
        }
    }
}

std::string capture_path() {
    if (Profiler* p = profiler()) {
        return p->capture_path();
    }
    return {};
}

void finish() noexcept {
    if (g_state.load(std::memory_order_acquire) == 1 && g_profiler != nullptr) {
        try {
            g_profiler->finish();
        } catch (...) {
        }
    }
}

}  // namespace galaxy::gx::ultra
