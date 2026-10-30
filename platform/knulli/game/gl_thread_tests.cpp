// SPDX-License-Identifier: GPL-3.0-only
// The GL thread's queue and wrappers (gl_thread.h) without a driver: fake entry points stand in for GL, so the test
// sees on which thread and in which order each call is made and what memory it read. Checks: calls run in order on the
// GL thread, client memory is copied at the call (changing it afterwards changes nothing), calls with an answer wait
// for it, names come from the pools, the ring wraps under a load of ~200 MB, fences and the work after them come in
// order, and a thread that does not record calls straight through.
#include "galaxy/gx/gles/gl_thread.h"
#include "galaxy/gx/gles/gles_context.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

namespace gl = galaxy::gx::gles::gl;
namespace glthread = galaxy::gx::gles::glthread;

namespace {

int failures = 0;
void expect(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

std::thread::id g_main;
std::atomic<int> g_off_main{0};
std::atomic<int> g_on_main{0};
std::vector<GLenum> g_enabled;  // GL thread
std::vector<std::uint8_t> g_uploaded;
GLint g_row_length = 0;
GLuint g_next_name = 100;

void note_thread() {
    if (std::this_thread::get_id() == g_main) ++g_on_main; else ++g_off_main;
}
void GL_APIENTRY fake_enable(GLenum cap) { note_thread(); g_enabled.push_back(cap); }
void GL_APIENTRY fake_pixel_store(GLenum pname, GLint value) {
    if (pname == GL_UNPACK_ROW_LENGTH) g_row_length = value;
}
void GL_APIENTRY fake_bind_buffer(GLenum, GLuint) {}
void GL_APIENTRY fake_tex_sub_image(GLenum, GLint, GLint, GLint, GLsizei w, GLsizei h, GLenum, GLenum type,
                                    const void* pixels) {
    note_thread();
    // RGBA8 (or a 16-bit compact texel) with the row length in force: the last row is w texels, the others row_length.
    const std::size_t texel = type == GL_UNSIGNED_BYTE ? 4u : 2u;
    const std::size_t row = static_cast<std::size_t>(g_row_length > 0 ? g_row_length : w) * texel;
    const std::size_t bytes = row * static_cast<std::size_t>(h - 1) + static_cast<std::size_t>(w) * texel;
    const auto* p = static_cast<const std::uint8_t*>(pixels);
    g_uploaded.assign(p, p + bytes);
}
void GL_APIENTRY fake_get_integer(GLenum, GLint* out) { note_thread(); *out = static_cast<GLint>(g_enabled.size()); }
void GL_APIENTRY fake_gen_textures(GLsizei n, GLuint* out) {
    note_thread();
    for (GLsizei i = 0; i < n; ++i) out[i] = g_next_name++;
}
std::vector<GLuint> g_deleted;
void GL_APIENTRY fake_delete_textures(GLsizei n, const GLuint* names) { g_deleted.assign(names, names + n); }
void GL_APIENTRY fake_finish() { note_thread(); }

}  // namespace

int main() {
    g_main = std::this_thread::get_id();
    gl::glEnable = fake_enable;
    gl::glPixelStorei = fake_pixel_store;
    gl::glBindBuffer = fake_bind_buffer;
    gl::glTexSubImage2D = fake_tex_sub_image;
    gl::glGetIntegerv = fake_get_integer;
    gl::glGenTextures = fake_gen_textures;
    gl::glDeleteTextures = fake_delete_textures;
    gl::glFinish = fake_finish;
    glthread::install();

    // Not recording yet: straight through on this thread.
    gl::glEnable(1u);
    expect(g_on_main.load() == 1 && g_enabled.size() == 1u, "sin hilo: la llamada va directa");

    expect(glthread::start_for_tests(), "el hilo arranca");
    for (GLenum cap = 2; cap < 1002; ++cap) gl::glEnable(cap);
    GLint seen = 0;
    gl::glGetIntegerv(0u, &seen);  // waits: everything before it has run
    expect(seen == 1001, "una llamada con respuesta espera a las anteriores");
    bool ordered = true;
    for (std::size_t i = 0; i < g_enabled.size(); ++i) ordered = ordered && g_enabled[i] == i + 1u;
    expect(ordered && g_off_main.load() == 1001, "en orden y en el hilo GL");

    // A texture upload with a row length: the bytes as they were at the call.
    std::vector<std::uint8_t> pixels(8u * 4u * 3u);
    for (std::size_t i = 0; i < pixels.size(); ++i) pixels[i] = static_cast<std::uint8_t>(i);
    const std::vector<std::uint8_t> original(pixels.begin(), pixels.begin() + 8u * 4u * 2u + 5u * 4u);
    gl::glPixelStorei(GL_UNPACK_ROW_LENGTH, 8);
    gl::glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 5, 3, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    std::memset(pixels.data(), 0xEE, pixels.size());
    gl::glFinish();
    expect(g_uploaded == original, "la subida copia la memoria del llamador (fila de 8, 5x3)");
    // A compact texture's (texture_compact.h): 16-bit texels, also copied at the call (no wait for the GL thread).
    for (std::size_t i = 0; i < pixels.size(); ++i) pixels[i] = static_cast<std::uint8_t>(i * 3u);
    const std::vector<std::uint8_t> original16(pixels.begin(), pixels.begin() + 8u * 2u * 2u + 5u * 2u);
    const int syncs_before = g_on_main.load();
    gl::glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 5, 3, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, pixels.data());
    std::memset(pixels.data(), 0xEE, pixels.size());
    gl::glFinish();
    expect(g_uploaded == original16 && g_on_main.load() == syncs_before,
           "la subida en RGB565 copia la memoria del llamador (fila de 8, 5x3)");

    // Names from the pool (refilled on the GL thread), then deletes with the array copied.
    GLuint names[3] = {};
    gl::glGenTextures(3, names);
    expect(names[0] >= 100u && names[1] != names[0] && names[2] != names[1], "nombres del pool");
    GLuint more[60] = {};
    gl::glGenTextures(60, more);  // past the pool: the rest made at once
    bool distinct = true;
    for (int i = 1; i < 60; ++i) distinct = distinct && more[i] != more[i - 1] && more[i] != 0u;
    expect(distinct, "mas nombres que el pool: todos distintos");
    GLuint doomed[2] = {names[0], names[1]};
    gl::glDeleteTextures(2, doomed);
    doomed[0] = doomed[1] = 0u;
    gl::glFinish();
    expect(g_deleted.size() == 2u && g_deleted[0] == names[0] && g_deleted[1] == names[1], "el borrado copia los nombres");

    // ~200 MB through the 8 MB ring (records of 1-4 KiB): it wraps many times and the order holds.
    std::atomic<std::uint64_t> next{0};
    std::atomic<bool> in_order{true};
    for (std::uint64_t i = 0; i < 80000; ++i) {
        std::array<std::uint8_t, 2500> blob{};
        blob[0] = static_cast<std::uint8_t>(i);
        glthread::enqueue([i, blob, &next, &in_order] {
            if (next.load() != i || blob[0] != static_cast<std::uint8_t>(i)) in_order = false;
            next.store(i + 1);
        });
    }
    glthread::run_sync([] {});
    expect(in_order.load() && next.load() == 80000u, "la cola da la vuelta en orden");

    // Fences: the work after one runs before completed() reports it.
    std::atomic<std::uint64_t> seen_at_job{~0ull};
    glthread::after_fence(1u, [&] { seen_at_job = glthread::completed(); });
    glthread::submit_fence(1u);
    expect(glthread::wait_fence(1u, 2'000'000'000ull) && glthread::completed() >= 1u, "la fence pasa");
    expect(seen_at_job.load() == 0u, "el trabajo de la fence corre antes de que se informe");
    std::atomic<bool> late{false};
    glthread::after_fence(1u, [&] { late = true; });  // already passed: runs when reached
    glthread::run_sync([] {});
    expect(late.load(), "un trabajo de una fence ya pasada corre al llegar");

    const glthread::Stats stats = glthread::stats();
    expect(stats.syncs >= 4u && stats.commands > 80000u, "estadisticas");
    glthread::stop();
    const int before = g_on_main.load();
    gl::glEnable(5000u);
    expect(g_on_main.load() == before + 1, "tras parar, directo otra vez");
    if (failures == 0) std::printf("gl_thread_tests: ok\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
