// SPDX-License-Identifier: GPL-3.0-only
// Nopal's debug channel (nopal_depurar.h): a server thread owns the sockets; the render thread only queues lines
// and, when paused, waits on a condition the server thread releases.
#include "galaxy/gx/gles/nopal_depurar.h"

#if defined(_WIN32)

namespace galaxy::gx::nopal_depurar {
bool active() noexcept { return false; }
void frame(const ultra::FrameSample&) noexcept {}
void scene(const char*) noexcept {}
int port() noexcept { return 0; }
void close() noexcept {}
}  // namespace galaxy::gx::nopal_depurar

#else

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

namespace galaxy::gx::nopal_depurar {
namespace {

constexpr std::size_t kQueueMax = 4096;  // lines waiting to be sent; more are dropped

struct Channel {
    std::mutex mutex;
    std::condition_variable resume;  // the render thread, paused
    std::deque<std::string> queue;
    bool connected = false;
    bool paused = false;
    unsigned steps = 0;  // frames `paso` lets through
    bool stopping = false;
    int listen_fd = -1;
    int client_fd = -1;
    int port = 0;
    int log_fd = -1;     // the real stderr while the log goes through the tee
    std::thread server;
};

Channel* g_channel = nullptr;
std::atomic<bool> g_active{false};
std::once_flag g_once;

void queue_line(Channel& c, std::string line) {
    const std::lock_guard<std::mutex> lock(c.mutex);
    if (!c.connected || c.queue.size() >= kQueueMax) return;
    line.push_back('\n');
    c.queue.push_back(std::move(line));
}

std::string state_line(const Channel& c) { return c.paused ? "ESTADO pausa" : "ESTADO sigue"; }

bool send_all(int fd, const std::string& text) {
    std::size_t done = 0;
    while (done < text.size()) {
        const ssize_t n = ::send(fd, text.data() + done, text.size() - done, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        done += static_cast<std::size_t>(n);
    }
    return true;
}

void command(Channel& c, const std::string& cmd) {
    bool reply = false;
    {
        const std::lock_guard<std::mutex> lock(c.mutex);
        if (cmd == "pausa") { c.paused = true; reply = true; }
        else if (cmd == "sigue") { c.paused = false; c.steps = 0; reply = true; }
        else if (cmd == "paso") { c.paused = true; ++c.steps; reply = true; }
        else if (cmd == "overlay") {
            c.queue.push_back("L nopal: overlay no disponible en Nebula (las medidas van en estas líneas F y R)\n");
        } else if (cmd == "salir") {
            c.paused = false;
            c.queue.push_back("L nopal: salir\n");
        }
        if (reply) c.queue.push_back(state_line(c) + "\n");
    }
    c.resume.notify_all();
    if (cmd == "salir") ::kill(::getpid(), SIGTERM);
}

void drop_client(Channel& c) {
    {
        const std::lock_guard<std::mutex> lock(c.mutex);
        if (c.client_fd >= 0) ::close(c.client_fd);
        c.client_fd = -1;
        c.connected = false;
        c.paused = false;  // nobody left to say sigue
        c.steps = 0;
        c.queue.clear();
    }
    c.resume.notify_all();
}

void serve(Channel& c, std::string title) {
    std::string input;
    for (;;) {
        int client = -1;
        {
            const std::lock_guard<std::mutex> lock(c.mutex);
            if (c.stopping) return;
            client = c.client_fd;
        }
        pollfd fds[1]{};
        fds[0].fd = client >= 0 ? client : c.listen_fd;
        fds[0].events = POLLIN;
        const int ready = ::poll(fds, 1, 15);
        if (client < 0) {
            if (ready > 0) {
                const int fd = ::accept(c.listen_fd, nullptr, nullptr);
                if (fd >= 0) {
                    int one = 1;
                    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
                    std::string hello;
                    {
                        const std::lock_guard<std::mutex> lock(c.mutex);
                        c.client_fd = fd;
                        c.connected = true;
                        c.queue.clear();
                        hello = "HOLA nopal 1 " + std::string(std::getenv("NEBULA_SIM") ? "sim " : "consola ") + title +
                                "\n" + state_line(c) + "\n";
                    }
                    input.clear();
                    if (!send_all(fd, hello)) drop_client(c);
                }
            }
            continue;
        }
        if (ready > 0) {
            char buffer[256];
            const ssize_t n = ::recv(client, buffer, sizeof buffer, MSG_DONTWAIT);
            if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
                drop_client(c);
                continue;
            }
            if (n > 0) input.append(buffer, static_cast<std::size_t>(n));
            std::size_t end;
            while ((end = input.find('\n')) != std::string::npos) {
                std::string cmd = input.substr(0, end);
                input.erase(0, end + 1u);
                if (!cmd.empty() && cmd.back() == '\r') cmd.pop_back();
                command(c, cmd);
            }
        }
        std::deque<std::string> out;
        {
            const std::lock_guard<std::mutex> lock(c.mutex);
            out.swap(c.queue);
        }
        std::string batch;
        for (const std::string& line : out) batch += line;
        if (!batch.empty() && !send_all(client, batch)) drop_client(c);
    }
}

// The log tee: stderr becomes a pipe whose reader writes everything on to the real stderr and queues each line.
void tee_stderr(Channel& c) {
    int fds[2];
    if (::pipe(fds) != 0) return;
    c.log_fd = ::dup(STDERR_FILENO);
    if (c.log_fd < 0 || ::dup2(fds[1], STDERR_FILENO) < 0) {
        ::close(fds[0]);
        ::close(fds[1]);
        return;
    }
    ::close(fds[1]);
    const int read_fd = fds[0], real = c.log_fd;
    std::thread([&c, read_fd, real] {
        std::string pending;
        char buffer[4096];
        for (;;) {
            const ssize_t n = ::read(read_fd, buffer, sizeof buffer);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) return;
            for (ssize_t done = 0; done < n;) {
                const ssize_t w = ::write(real, buffer + done, static_cast<std::size_t>(n - done));
                if (w <= 0) break;
                done += w;
            }
            pending.append(buffer, static_cast<std::size_t>(n));
            std::size_t end;
            while ((end = pending.find('\n')) != std::string::npos) {
                queue_line(c, "L " + pending.substr(0, end));
                pending.erase(0, end + 1u);
            }
            if (pending.size() > 4096u) {
                queue_line(c, "L " + pending);
                pending.clear();
            }
        }
    }).detach();
}

void open_channel() {
    const char* value = std::getenv("NOPAL_DEPURAR");
    if (value == nullptr || value[0] == '\0') return;
    const bool any_port = std::strcmp(value, "auto") == 0;  // tests: a free port
    const long port = any_port ? 0 : std::strtol(value, nullptr, 10);
    if (!any_port && (port <= 0 || port > 65535)) return;
    auto* c = new Channel();
    c->listen_fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (c->listen_fd < 0) {
        delete c;
        return;
    }
    int one = 1;
    ::setsockopt(c->listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<std::uint16_t>(port));
    address.sin_addr.s_addr = htonl(INADDR_ANY);  // also by WiFi: the Estudio on the PC, Nebula on the console
    socklen_t length = sizeof address;
    if (::bind(c->listen_fd, reinterpret_cast<sockaddr*>(&address), sizeof address) != 0 ||
        ::listen(c->listen_fd, 1) != 0 ||
        ::getsockname(c->listen_fd, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
        std::fprintf(stderr, "[nopal] depurar: no se pudo escuchar en el puerto %ld\n", port);
        ::close(c->listen_fd);
        delete c;
        return;
    }
    c->port = ntohs(address.sin_port);
    const char* title = std::getenv("NOPAL_TITULO");
    std::string name = title != nullptr && title[0] != '\0' ? title : "Super Mario Galaxy (Nebula)";
    std::fprintf(stderr, "[nopal] depurar: esperando al Depurador de Nopal Estudio en el puerto %d\n", c->port);
    const char* tee = std::getenv("NOPAL_DEPURAR_LOG");
    if (tee == nullptr || std::strcmp(tee, "0") != 0) tee_stderr(*c);
    c->server = std::thread(serve, std::ref(*c), std::move(name));
    g_channel = c;
    g_active.store(true, std::memory_order_release);
}

double ram_mib() {
    long total = 0, resident = 0;
    if (FILE* f = std::fopen("/proc/self/statm", "r")) {
        if (std::fscanf(f, "%ld %ld", &total, &resident) != 2) resident = 0;
        std::fclose(f);
    }
    return static_cast<double>(resident) * static_cast<double>(::sysconf(_SC_PAGESIZE)) / (1024.0 * 1024.0);
}

std::int64_t process_cpu_ns() {
    timespec t{};
    ::clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t);
    return static_cast<std::int64_t>(t.tv_sec) * 1000000000 + t.tv_nsec;
}

}  // namespace

bool active() noexcept {
    std::call_once(g_once, [] {
        try {
            open_channel();
        } catch (...) {
        }
    });
    return g_active.load(std::memory_order_acquire);
}

void frame(const ultra::FrameSample& s) noexcept {
    if (!g_active.load(std::memory_order_relaxed)) return;
    Channel& c = *g_channel;
    static std::int64_t previous_present = 0, previous_cpu = 0, previous_cpu_wall = 0;
    static std::uint64_t presents = 0;
    try {
        {
            const std::lock_guard<std::mutex> lock(c.mutex);
            if (!c.connected) {
                previous_present = s.present_ns;
                return;
            }
        }
        const double frame_ms = previous_present != 0 ? static_cast<double>(s.present_ns - previous_present) / 1e6 : 0.0;
        previous_present = s.present_ns;
        const auto ms = [](std::int64_t ns) { return static_cast<double>(ns) / 1e6; };
        char line[320];
        int n = std::snprintf(line, sizeof line, "F %llu %.3f %.3f %u dibujo=%.3f espera_gpu=%.3f vsync=%.3f",
                              static_cast<unsigned long long>(s.frame_index), frame_ms, ms(s.cpu_ns),
                              1u + s.repeats, ms(s.draw_ns), ms(s.slot_wait_ns), ms(s.swap_wait_ns));
        if (s.readback_ns != 0 && n > 0 && n < static_cast<int>(sizeof line)) {
            n += std::snprintf(line + n, sizeof line - static_cast<std::size_t>(n), " lecturas_efb=%.3f",
                               ms(s.readback_ns));
        }
        if (s.gl_thread && n > 0 && n < static_cast<int>(sizeof line)) {
            std::snprintf(line + n, sizeof line - static_cast<std::size_t>(n), " hilo_gl=%.3f espera_hilo_gl=%.3f",
                          ms(s.gl_cpu_ns), ms(s.gl_sync_wait_ns));
        }
        queue_line(c, line);
        if (++presents % 30u == 0u) {
            const std::int64_t cpu = process_cpu_ns();
            const std::int64_t wall = s.present_ns;
            const double pct = previous_cpu_wall != 0 && wall > previous_cpu_wall
                ? 100.0 * static_cast<double>(cpu - previous_cpu) / static_cast<double>(wall - previous_cpu_wall)
                : 0.0;
            previous_cpu = cpu;
            previous_cpu_wall = wall;
            std::snprintf(line, sizeof line, "R %.1f %.1f", ram_mib(), pct);
            queue_line(c, line);
        }
        // Paused: wait here (after this frame's line) until sigue, a paso, salir or the Estudio leaving.
        std::unique_lock<std::mutex> lock(c.mutex);
        if (c.paused && c.steps > 0u) {
            --c.steps;
            return;
        }
        c.resume.wait(lock, [&c] { return !c.paused || c.steps > 0u || !c.connected || c.stopping; });
        if (c.steps > 0u) --c.steps;
    } catch (...) {
    }
}

void scene(const char* name) noexcept {
    if (!g_active.load(std::memory_order_relaxed) || name == nullptr) return;
    try {
        queue_line(*g_channel, std::string("L escena: ") + name);
    } catch (...) {
    }
}

int port() noexcept { return g_active.load(std::memory_order_acquire) ? g_channel->port : 0; }

void close() noexcept {
    if (!g_active.exchange(false)) return;
    Channel& c = *g_channel;
    {
        const std::lock_guard<std::mutex> lock(c.mutex);
        c.stopping = true;
    }
    c.resume.notify_all();
    if (c.server.joinable()) c.server.join();
    if (c.client_fd >= 0) ::close(c.client_fd);
    if (c.listen_fd >= 0) ::close(c.listen_fd);
    if (c.log_fd >= 0) ::dup2(c.log_fd, STDERR_FILENO);  // the reader sees the end of its pipe and returns
    // The channel stays allocated: the log reader may still hold it.
}

}  // namespace galaxy::gx::nopal_depurar

#endif
