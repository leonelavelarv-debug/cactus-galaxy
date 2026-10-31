// SPDX-License-Identifier: GPL-3.0-only
// Nopal's debug channel (nopal_depurar.h) without a GPU: a client on localhost in place of Nopal Estudio's Depurador.
// Checks: the greeting, a line per frame with its zones, memory and CPU every 30 frames, the log tee, pausa holding
// the render thread until paso lets exactly one frame through and sigue the rest, and the Estudio leaving releasing a
// pause. Exit status 0 when all pass.
#include "galaxy/gx/gles/nopal_depurar.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

namespace dep = galaxy::gx::nopal_depurar;
namespace ultra = galaxy::gx::ultra;

namespace {

int g_failures = 0;
void expect(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

struct Client {
    int fd = -1;
    std::string buffer;
    // The next line that starts with `prefix` (others skipped), within `ms`; empty when none came.
    std::string wait_for(const std::string& prefix, int ms = 2000) {
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        for (;;) {
            std::size_t nl;
            while ((nl = buffer.find('\n')) != std::string::npos) {
                std::string line = buffer.substr(0, nl);
                buffer.erase(0, nl + 1u);
                if (line.rfind(prefix, 0) == 0) return line;
            }
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(end - std::chrono::steady_clock::now());
            if (left.count() <= 0) return {};
            pollfd p{fd, POLLIN, 0};
            if (::poll(&p, 1, static_cast<int>(left.count())) <= 0) return {};
            char chunk[1024];
            const ssize_t n = ::recv(fd, chunk, sizeof chunk, 0);
            if (n <= 0) return {};
            buffer.append(chunk, static_cast<std::size_t>(n));
        }
    }
    void say(const char* text) {
        const std::string line = std::string(text) + "\n";
        (void)::send(fd, line.data(), line.size(), MSG_NOSIGNAL);
    }
};

Client connect_to(int port) {
    Client c;
    c.fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(static_cast<std::uint16_t>(port));
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(c.fd, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) {
        ::close(c.fd);
        c.fd = -1;
    }
    return c;
}

std::int64_t g_now = 1'000'000'000;
std::uint64_t g_index = 0;
ultra::FrameSample sample() {
    ultra::FrameSample s{};
    s.frame_index = ++g_index;
    g_now += 16'700'000;
    s.present_ns = g_now;
    s.cpu_ns = 9'000'000;
    s.draw_ns = 6'000'000;
    s.slot_wait_ns = 1'500'000;
    s.swap_wait_ns = 500'000;
    s.gl_thread = true;
    s.gl_cpu_ns = 4'000'000;
    return s;
}

// Calls frame() on another thread; true when it returned within `ms` (false: still held by a pause).
bool frame_returns(int ms, std::thread& held, std::atomic<bool>& done) {
    done = false;
    held = std::thread([&done] {
        dep::frame(sample());
        done = true;
    });
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (!done && std::chrono::steady_clock::now() < end) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    if (done) held.join();
    return done;
}

}  // namespace

// --servir <s>: Nebula de mentira para probar el Depurador de Nopal Estudio contra este canal: con NOPAL_DEPURAR=<puerto>,
// un frame cada 16,7 ms durante <s> segundos (pausa, paso y sigue funcionan como en el juego).
int serve(int seconds) {
    if (!dep::active()) {
        std::printf("FAIL NOPAL_DEPURAR\n");
        return 2;
    }
    std::printf("escuchando en %d\n", dep::port());
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (std::chrono::steady_clock::now() < end) {
        ultra::FrameSample s = sample();
        s.present_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                           std::chrono::steady_clock::now().time_since_epoch()).count();
        dep::frame(s);
        std::this_thread::sleep_for(std::chrono::microseconds(16700));
    }
    dep::close();
    return 0;
}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    if (argc >= 3 && std::string(argv[1]) == "--servir") return serve(std::atoi(argv[2]));
    setenv("NOPAL_DEPURAR", "auto", 1);
    setenv("NOPAL_TITULO", "Prueba", 1);
    expect(dep::active() && dep::port() > 0, "el canal escucha (NOPAL_DEPURAR=auto)");
    dep::frame(sample());  // nobody connected: nothing queued, nothing held

    Client c = connect_to(dep::port());
    expect(c.fd >= 0, "el Depurador se conecta");
    const std::string hello = c.wait_for("HOLA");
    expect(hello == "HOLA nopal 1 consola Prueba", "saludo del protocolo de Nopal");
    expect(c.wait_for("ESTADO") == "ESTADO sigue", "estado inicial: sigue");

    for (int i = 0; i < 30; ++i) dep::frame(sample());
    const std::string f = c.wait_for("F ");
    expect(f.find(" 16.700 9.000 1 dibujo=6.000 espera_gpu=1.500 vsync=0.500") != std::string::npos &&
               f.find("hilo_gl=4.000") != std::string::npos,
           "línea F: frame, CPU, pasos y zonas en ms");
    const std::string r = c.wait_for("R ");
    double ram = 0, cpu = -1;
    expect(std::sscanf(r.c_str(), "R %lf %lf", &ram, &cpu) == 2 && ram > 0.5, "línea R: RAM y CPU cada 30 frames");

    std::fprintf(stderr, "[prueba] una línea del registro\n");
    expect(c.wait_for("L [prueba]") == "L [prueba] una línea del registro", "el registro (stderr) llega como L");
    dep::scene("TestGalaxy");
    expect(c.wait_for("L escena") == "L escena: TestGalaxy", "la escena del juego llega como L");

    c.say("pausa");
    expect(c.wait_for("ESTADO pausa") == "ESTADO pausa", "pausa contestada");
    std::thread held;
    std::atomic<bool> done{false};
    expect(!frame_returns(150, held, done), "en pausa el hilo de render se detiene tras su present");
    c.say("paso");
    c.wait_for("ESTADO pausa");  // the reply to paso (so a later wait sees the next reply, not this one)
    const auto t0 = std::chrono::steady_clock::now();
    while (!done && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(2)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    expect(done.load(), "paso deja pasar el frame detenido");
    if (held.joinable()) held.join();
    expect(!frame_returns(150, held, done), "y el siguiente vuelve a detenerse");
    c.say("sigue");
    c.wait_for("ESTADO sigue");
    const auto t1 = std::chrono::steady_clock::now();
    while (!done && std::chrono::steady_clock::now() - t1 < std::chrono::seconds(2)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    expect(done.load(), "sigue lo suelta");
    if (held.joinable()) held.join();
    expect(frame_returns(500, held, done), "sin pausa los frames no esperan");

    c.say("pausa");
    c.wait_for("ESTADO pausa");  // not a stale "ESTADO sigue" from before
    expect(!frame_returns(150, held, done), "pausa otra vez");
    ::close(c.fd);
    const auto t2 = std::chrono::steady_clock::now();
    while (!done && std::chrono::steady_clock::now() - t2 < std::chrono::seconds(2)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    expect(done.load(), "si el Depurador se va, la pausa se suelta");
    if (held.joinable()) held.join();

    Client again = connect_to(dep::port());
    expect(again.wait_for("HOLA").rfind("HOLA nopal 1", 0) == 0, "se puede volver a conectar");
    ::close(again.fd);
    dep::close();
    std::printf("nopal depurar tests: %d failed\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
