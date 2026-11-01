// SPDX-License-Identifier: GPL-3.0-only
// The shared OpenGL ES context (gles_context.h) and the GL entry points (gl_api.h).

#include "galaxy/gx/gles/gles_context.h"
#include "galaxy/gx/gles/gl_thread.h"
#include "galaxy/gx/gles/shader_translate_gles.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <new>
#include <set>
#include <stdexcept>

namespace galaxy::gx::gles {

namespace gl {

#define GLES_DEFINE(type, name) type name = nullptr;
GLES_CORE_FUNCTIONS(GLES_DEFINE)
GLES_OPTIONAL_FUNCTIONS(GLES_DEFINE)
#undef GLES_DEFINE

namespace {
std::set<std::string> g_extensions;
}

bool load(Loader loader, std::string& missing) {
    missing.clear();
#define GLES_LOAD_CORE(type, name)                                     \
    name = reinterpret_cast<type>(loader(#name));                      \
    if (name == nullptr && missing.empty()) missing = #name;
#define GLES_LOAD_OPTIONAL(type, name) name = reinterpret_cast<type>(loader(#name));
    GLES_CORE_FUNCTIONS(GLES_LOAD_CORE)
    GLES_OPTIONAL_FUNCTIONS(GLES_LOAD_OPTIONAL)
#undef GLES_LOAD_CORE
#undef GLES_LOAD_OPTIONAL
    if (!missing.empty()) {
        return false;
    }
    g_extensions.clear();
    GLint count = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &count);
    for (GLint i = 0; i < count; ++i) {
        if (const GLubyte* name = glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i))) {
            g_extensions.insert(reinterpret_cast<const char*>(name));
        }
    }
    return true;
}

bool has_extension(const char* name) {
    return g_extensions.contains(name);
}

}  // namespace gl

using namespace gl;

namespace {

const char* string_of(GLenum name) {
    const GLubyte* value = glGetString(name);
    return value != nullptr ? reinterpret_cast<const char*>(value) : "";
}

void GL_APIENTRY debug_callback(GLenum, GLenum type, GLuint, GLenum severity, GLsizei, const GLchar* message,
                                const void*) {
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION_KHR) {
        return;
    }
    static std::atomic<unsigned> reported{0};
    if (reported.fetch_add(1u, std::memory_order_relaxed) < 64u) {
        std::fprintf(stderr, "[gles-debug] %s: %s\n", type == GL_DEBUG_TYPE_ERROR_KHR ? "error" : "note", message);
    }
}

}  // namespace

Texture::~Texture() {
    if (allocation_bytes != 0u) {
        Context::live_texture_bytes.fetch_sub(allocation_bytes, std::memory_order_relaxed);
        Context::live_texture_count.fetch_sub(1u, std::memory_order_relaxed);
    }
    const GLuint texture = name;
    const GLuint fb = framebuffer;
    if (texture == 0u && fb == 0u) {
        return;
    }
    Context::get().defer([texture, fb] {
        if (fb != 0u) glDeleteFramebuffers(1, &fb);
        if (texture != 0u) glDeleteTextures(1, &texture);
    });
}

Buffer::~Buffer() {
    if (size != 0u) {
        Context::live_buffer_bytes.fetch_sub(size, std::memory_order_relaxed);
    }
    const GLuint buffer = name;
    const std::uint64_t gx_address = address;
    const bool was_mapped = mapped != nullptr && !copy;
    // The GL thread reads the copy when a queued write runs (glthread::write_buffer): it lives until the deferred
    // deletion, after a fence past every call queued now.
    std::shared_ptr<std::byte> keep_copy;
    if (copy) {
        Context::get().forget_writes(this);
        keep_copy.reset(copy.release(), FreeCopy{});
    }
    if (buffer == 0u) {
        return;
    }
    Context::get().defer([buffer, gx_address, was_mapped, keep_copy] {
        if (was_mapped) {
            glBindBuffer(GL_COPY_WRITE_BUFFER, buffer);
            glUnmapBuffer(GL_COPY_WRITE_BUFFER);
            glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
        }
        glDeleteBuffers(1, &buffer);
        if (gx_address != 0u) {
            Context::get().release_address(gx_address);
        }
    });
}

Context& Context::get() noexcept {
    static Context context;
    return context;
}

bool Context::initialize(Loader loader) {
    if (ready_) {
        return true;
    }
    std::string missing;
    if (!gl::load(loader, missing)) {
        failure_ = "the OpenGL ES driver lacks " + missing + " (OpenGL ES 3.0 is needed)";
        std::fprintf(stderr, "[gles] %s\n", failure_.c_str());
        return false;
    }
    if (const char* threaded = std::getenv("GALAXY_GLES_THREAD"); threaded != nullptr && threaded[0] == '1') {
        glthread::install();  // the wrappers pass through until the render thread starts the GL thread
    }
    caps_.vendor = string_of(GL_VENDOR);
    caps_.renderer = string_of(GL_RENDERER);
    caps_.version = string_of(GL_VERSION);
    glGetIntegerv(GL_MAJOR_VERSION, &caps_.version_major);
    glGetIntegerv(GL_MINOR_VERSION, &caps_.version_minor);
    // OpenGL ES 3.0 is the base; what 3.1 and 3.2 add (storage and texture buffers, vertex attribute bindings) and
    // EXT_buffer_storage are used when the driver has them. GALAXY_GLES_FORCE_ES30=1 tries the 3.0 path on any.
    if (const char* force = std::getenv("GALAXY_GLES_FORCE_ES30"); force != nullptr && force[0] == '1') {
        caps_.forced_es30 = true;
    }
    const bool es30 = caps_.version_major >= 3;
    const bool es31 = !caps_.forced_es30 &&
        (caps_.version_major > 3 || (caps_.version_major == 3 && caps_.version_minor >= 1));
    const bool es32 = !caps_.forced_es30 &&
        (caps_.version_major > 3 || (caps_.version_major == 3 && caps_.version_minor >= 2));
    if (!es30) {
        failure_ = "OpenGL ES 3.0 is needed (the driver has " + caps_.version + ")";
        std::fprintf(stderr, "[gles] %s\n", failure_.c_str());
        return false;
    }
    if (caps_.forced_es30) {
        caps_.version_major = 3;
        caps_.version_minor = 0;  // GLSL ES 3.00 and every 3.0 fallback (the log keeps the driver's own version)
    }
    caps_.buffer_storage = !caps_.forced_es30 && glBufferStorageEXT != nullptr && has_extension("GL_EXT_buffer_storage");
    if (const char* off = std::getenv("GALAXY_GLES_BUFFER_STORAGE"); off != nullptr && off[0] == '0') {
        caps_.buffer_storage = false;
    }
    caps_.vertex_attrib_binding = es31 && glVertexAttribFormat != nullptr && glVertexAttribIFormat != nullptr &&
        glVertexAttribBinding != nullptr && glBindVertexBuffer != nullptr;
    if (es31) {
        GLint vertex_blocks = 0;
        glGetIntegerv(GL_MAX_VERTEX_SHADER_STORAGE_BLOCKS, &vertex_blocks);
        caps_.vertex_storage_blocks = vertex_blocks >= 1;
        glGetIntegerv(GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT, &caps_.storage_offset_alignment);
    }
    // GALAXY_GLES_PALETTE=texture: a texture buffer even with vertex storage blocks; =2d: the 2D texture of 3.0.
    const char* palette = std::getenv("GALAXY_GLES_PALETTE");
    if (palette != nullptr && (std::strcmp(palette, "texture") == 0 || std::strcmp(palette, "2d") == 0)) {
        caps_.vertex_storage_blocks = false;
    }
    if (glTexBufferRange == nullptr && glTexBufferRangeEXT != nullptr) {
        glTexBufferRange = glTexBufferRangeEXT;
    }
    caps_.texture_buffer = es31 && glTexBufferRange != nullptr && (es32 || has_extension("GL_EXT_texture_buffer")) &&
        !(palette != nullptr && std::strcmp(palette, "2d") == 0);
    caps_.palette_texture_2d = !caps_.vertex_storage_blocks && !caps_.texture_buffer;
    caps_.glsl_version = glsl_version_for(caps_.version_major, caps_.version_minor);
    if (caps_.palette_texture_2d || caps_.glsl_version == 300) {
        // The 2D palette is GLSL ES 3.00's form (shader_translate_gles.h), and 3.00 has no other.
        caps_.glsl_version = 300;
        caps_.palette_texture_2d = true;
        caps_.vertex_storage_blocks = false;
        caps_.texture_buffer = false;
    }
    caps_.timer_query = glQueryCounterEXT != nullptr && glGetQueryObjectui64vEXT != nullptr &&
        has_extension("GL_EXT_disjoint_timer_query");
    caps_.framebuffer_fetch_arm = has_extension("GL_ARM_shader_framebuffer_fetch");
    caps_.framebuffer_fetch_ext = has_extension("GL_EXT_shader_framebuffer_fetch");
    GLint binary_formats = 0;
    glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &binary_formats);
    caps_.program_binary = binary_formats > 0;
    glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &caps_.uniform_offset_alignment);
    if (caps_.texture_buffer) {
        glGetIntegerv(GL_TEXTURE_BUFFER_OFFSET_ALIGNMENT, &caps_.texture_buffer_offset_alignment);
    }
    glGetIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE, &caps_.max_uniform_block_bytes);
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &caps_.max_texture_size);
    glGetIntegerv(GL_MAX_SAMPLES, &caps_.max_samples);
    glGetIntegerv(GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS, &caps_.max_vertex_texture_units);
    caps_.debug_output = glDebugMessageCallbackKHR != nullptr && has_extension("GL_KHR_debug");
    if (!caps_.vertex_storage_blocks && !caps_.texture_buffer && caps_.max_vertex_texture_units < 1) {
        failure_ = "the OpenGL ES driver reads no textures in vertex shaders (the matrix palette)";
        std::fprintf(stderr, "[gles] %s\n", failure_.c_str());
        return false;
    }
    {
        const char* value = std::getenv("GALAXY_GLES_CHECK");
        checking_ = value != nullptr && value[0] == '1';
    }
    if (checking_ && caps_.debug_output) {
        glEnable(GL_DEBUG_OUTPUT_KHR);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS_KHR);
        glDebugMessageCallbackKHR(debug_callback, nullptr);
    }
    set_owner_thread();
    ready_ = true;
    std::fprintf(stderr,
                 "[gles] \"%s\" by \"%s\", %s; palette from %s, UBO align %d, SSBO align %d, max UBO %d, "
                 "framebuffer fetch %s, program binaries %s, timer queries %s, buffers %s, vertex attributes %s, "
                 "OpenGL ES %d.%d context%s\n",
                 caps_.renderer.c_str(), caps_.vendor.c_str(), caps_.version.c_str(),
                 caps_.vertex_storage_blocks ? "a storage buffer"
                     : caps_.texture_buffer  ? "a texture buffer"
                                             : "a 2D texture",
                 caps_.uniform_offset_alignment, caps_.storage_offset_alignment, caps_.max_uniform_block_bytes,
                 caps_.framebuffer_fetch_arm ? "ARM" : caps_.framebuffer_fetch_ext ? "EXT" : "no",
                 caps_.program_binary ? "yes" : "no", caps_.timer_query ? "yes" : "no",
                 caps_.buffer_storage ? "persistent" : "copied", caps_.vertex_attrib_binding ? "bindings" : "pointers",
                 caps_.version_major, caps_.version_minor, caps_.forced_es30 ? " (GALAXY_GLES_FORCE_ES30)" : "");
    return true;
}

void Context::shutdown() noexcept {
    if (!ready_) {
        return;
    }
    if (on_owner_thread()) {
        glFinish();
        for (const Pending& pending : pending_) {
            glDeleteSync(pending.sync);
        }
        pending_.clear();
        completed_.store(submitted_.load());
        collect();
    }
    ready_ = false;
}

std::uint64_t Context::submit() {
    std::lock_guard<std::mutex> lock(submit_mutex_);
    const std::uint64_t value = submitted_.load(std::memory_order_relaxed) + 1u;
    if (glthread::recording()) {
        // The GL thread inserts it after the commands queued so far.
        glthread::submit_fence(value);
        submitted_.store(value, std::memory_order_release);
        return value;
    }
    GLsync sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    glFlush();
    pending_.push_back({value, sync});
    submitted_.store(value, std::memory_order_release);
    return value;
}

std::uint64_t Context::completed_value() {
    if (glthread::active()) {
        const std::uint64_t done = glthread::completed();
        completed_.store(done, std::memory_order_release);
        return done;
    }
    if (!ready_ || !on_owner_thread()) {
        return completed_.load(std::memory_order_acquire);
    }
    std::lock_guard<std::mutex> lock(submit_mutex_);
    std::size_t done = 0;
    for (; done < pending_.size(); ++done) {
        const GLenum status = glClientWaitSync(pending_[done].sync, 0, 0);
        if (status != GL_ALREADY_SIGNALED && status != GL_CONDITION_SATISFIED) {
            break;
        }
        completed_.store(pending_[done].value, std::memory_order_release);
        glDeleteSync(pending_[done].sync);
    }
    pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(done));
    return completed_.load(std::memory_order_acquire);
}

bool Context::wait(std::uint64_t value, std::uint64_t timeout_ns) {
    if (completed_value() >= value) {
        return true;
    }
    if (glthread::active()) {
        const bool ok = glthread::wait_fence(value, timeout_ns);
        completed_.store(glthread::completed(), std::memory_order_release);
        return ok;
    }
    if (!on_owner_thread()) {
        // Another thread waits for the owner's polls.
        const auto start = std::chrono::steady_clock::now();
        while (completed_.load(std::memory_order_acquire) < value) {
            std::this_thread::sleep_for(std::chrono::microseconds(200));
            if (timeout_ns != UINT64_MAX && std::chrono::steady_clock::now() - start >
                                                std::chrono::nanoseconds(timeout_ns)) {
                return false;
            }
        }
        return true;
    }
    std::lock_guard<std::mutex> lock(submit_mutex_);
    while (!pending_.empty() && pending_.front().value <= value) {
        const GLenum status = glClientWaitSync(pending_.front().sync, GL_SYNC_FLUSH_COMMANDS_BIT,
                                               timeout_ns == UINT64_MAX ? GL_TIMEOUT_IGNORED : timeout_ns);
        if (status == GL_TIMEOUT_EXPIRED || status == GL_WAIT_FAILED) {
            return false;
        }
        completed_.store(pending_.front().value, std::memory_order_release);
        glDeleteSync(pending_.front().sync);
        pending_.erase(pending_.begin());
    }
    return completed_.load(std::memory_order_acquire) >= value;
}

std::vector<glthread::PendingFence> Context::take_pending_fences() {
    std::lock_guard<std::mutex> lock(submit_mutex_);
    std::vector<glthread::PendingFence> out;
    out.reserve(pending_.size());
    for (const Pending& pending : pending_) {
        out.push_back({pending.value, pending.sync});
    }
    pending_.clear();
    return out;
}

void Context::adopt_completed(std::uint64_t value) noexcept {
    std::lock_guard<std::mutex> lock(submit_mutex_);
    completed_.store(std::max(completed_.load(std::memory_order_relaxed), value), std::memory_order_release);
}

void Context::defer(std::function<void()> destroy) {
    std::lock_guard<std::mutex> lock(deferred_mutex_);
    deferred_.push_back({submitted_.load(std::memory_order_acquire) + 1u, std::move(destroy)});
}

void Context::collect() {
    if (!on_owner_thread()) {
        return;
    }
    const std::uint64_t done = completed_value();
    std::vector<Deferred> due;
    {
        std::lock_guard<std::mutex> lock(deferred_mutex_);
        auto split = std::stable_partition(deferred_.begin(), deferred_.end(),
                                           [done](const Deferred& d) { return d.value > done; });
        due.assign(std::make_move_iterator(split), std::make_move_iterator(deferred_.end()));
        deferred_.erase(split, deferred_.end());
    }
    for (Deferred& d : due) {
        d.destroy();
    }
}

std::shared_ptr<Texture> Context::create_texture(std::uint32_t width, std::uint32_t height, std::uint32_t levels,
                                                 GLenum format, bool render_target, const char*) {
    if (width == 0u || height == 0u) {
        return {};
    }
    auto texture = std::make_shared<Texture>();
    glGenTextures(1, &texture->name);
    glBindTexture(GL_TEXTURE_2D, texture->name);
    glTexStorage2D(GL_TEXTURE_2D, static_cast<GLsizei>(std::max(1u, levels)), format, static_cast<GLsizei>(width),
                   static_cast<GLsizei>(height));
    // Sampler objects decide filtering; these only keep a texture complete when none is bound.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, levels > 1u ? GL_LINEAR_MIPMAP_NEAREST : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // Compact intensity textures (texture_compact.h) sample as the guest's: R8 as (i, i, i, i), RG8 as (i, i, i, a).
    if (format == GL_R8 || format == GL_RG8) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_RED);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, GL_RED);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, format == GL_R8 ? GL_RED : GL_GREEN);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    if (glGetError() != GL_NO_ERROR) {
        // The deleter frees the name.
        return {};
    }
    texture->format = format;
    texture->width = width;
    texture->height = height;
    texture->levels = std::max(1u, levels);
    texture->render_target = render_target;
    const std::uint64_t texel = format == GL_RGBA16F ? 8u
        : format == GL_R8 ? 1u
        : (format == GL_RG8 || format == GL_RGB565 || format == GL_RGB5_A1 || format == GL_RGBA4) ? 2u
        : 4u;
    std::uint64_t bytes = 0;
    for (std::uint32_t l = 0; l < texture->levels; ++l) {
        bytes += static_cast<std::uint64_t>(std::max(1u, width >> l)) * std::max(1u, height >> l) * texel;
    }
    texture->allocation_bytes = bytes;
    live_texture_bytes.fetch_add(bytes, std::memory_order_relaxed);
    live_texture_count.fetch_add(1u, std::memory_order_relaxed);
    return texture;
}

std::shared_ptr<Buffer> Context::create_buffer(std::uint64_t size, const char* name, bool readable) {
    if (size == 0u) {
        return {};
    }
    auto buffer = std::make_shared<Buffer>();
    glGenBuffers(1, &buffer->name);
    glBindBuffer(GL_COPY_WRITE_BUFFER, buffer->name);
    // GALAXY_GLES_INDEX_COPY=1 (a test): the index ring with a CPU copy even with EXT_buffer_storage, sent with a map
    // per range like OpenGL ES 3.0, so the driver sees each change (docs/GX_ES_MEMORY_GROWTH.md).
    static const bool index_copy = [] {
        const char* value = std::getenv("GALAXY_GLES_INDEX_COPY");
        return value != nullptr && value[0] == '1';
    }();
    const bool force_copy = index_copy && caps_.buffer_storage && name != nullptr && std::strcmp(name, "index") == 0;
    if (force_copy) {
        copied_buffers_.store(true, std::memory_order_relaxed);
    }
    if (!caps_.buffer_storage || force_copy) {
        // OpenGL ES 3.0: the CPU writes a copy; flush_writes sends what changed before the draws that read it.
        // calloc, not new + memset: zero without touching the pages, so the copy costs what is written.
        buffer->copy.reset(static_cast<std::byte*>(std::calloc(static_cast<std::size_t>(size), 1u)));
        if (!buffer->copy) {
            glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
            std::fprintf(stderr, "[gles] no memory for the %llu-byte CPU copy of buffer '%s'\n",
                         static_cast<unsigned long long>(size), name != nullptr ? name : "");
            return {};
        }
        glBufferData(GL_COPY_WRITE_BUFFER, static_cast<GLsizeiptr>(size), nullptr, GL_DYNAMIC_DRAW);
        glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
        std::uint64_t all = 0;
        {
            std::lock_guard<std::mutex> lock(dirty_mutex_);
            all = copied_bytes_ += size;
        }
        if (size >= (8u << 20u)) {
            std::fprintf(stderr, "[gles] buffer '%s' copied: %llu MiB (%llu MiB of copies in all; pages committed as "
                         "written)\n", name != nullptr ? name : "", static_cast<unsigned long long>(size >> 20u),
                         static_cast<unsigned long long>(all >> 20u));
        }
        buffer->mapped = buffer->copy.get();
        buffer->size = size;
        live_buffer_bytes.fetch_add(size, std::memory_order_relaxed);
        buffer->address = register_address(buffer.get());
        (void)readable;  // nothing reads these back on the CPU (readbacks use their own pixel-pack buffers)
        return buffer;
    }
    const GLbitfield access = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT_EXT | GL_MAP_COHERENT_BIT_EXT |
        (readable ? GL_MAP_READ_BIT : 0u);
    glBufferStorageEXT(GL_COPY_WRITE_BUFFER, static_cast<GLsizeiptr>(size), nullptr,
                       access | (readable ? GL_CLIENT_STORAGE_BIT_EXT : 0u));
    void* mapped = glMapBufferRange(GL_COPY_WRITE_BUFFER, 0, static_cast<GLsizeiptr>(size), access);
    glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
    if (mapped == nullptr) {
        std::fprintf(stderr, "[gles] a %llu-byte persistent buffer could not be mapped (GL error 0x%x)\n",
                     static_cast<unsigned long long>(size), glGetError());
        return {};
    }
    buffer->mapped = static_cast<std::byte*>(mapped);
    buffer->size = size;
    live_buffer_bytes.fetch_add(size, std::memory_order_relaxed);
    buffer->address = register_address(buffer.get());
    return buffer;
}

std::uint64_t Context::register_address(Buffer* buffer) {
    std::lock_guard<std::mutex> lock(address_mutex_);
    std::uint32_t id = 0;
    if (!free_address_ids_.empty()) {
        id = free_address_ids_.back();
        free_address_ids_.pop_back();
    } else {
        if (next_address_id_ >= kAddressSlots) {
            throw std::runtime_error("GLES GX address space exhausted");
        }
        id = next_address_id_++;
    }
    address_slots_[id].store(buffer, std::memory_order_release);
    return static_cast<std::uint64_t>(id) << kAddressShift;
}

void Context::release_address(std::uint64_t address) noexcept {
    const std::uint64_t id = address >> kAddressShift;
    if (id == 0u || id >= kAddressSlots) {
        return;
    }
    std::lock_guard<std::mutex> lock(address_mutex_);
    address_slots_[id].store(nullptr, std::memory_order_release);
    free_address_ids_.push_back(static_cast<std::uint32_t>(id));
}

void Context::note_write(std::uint64_t address, std::uint64_t bytes) noexcept {
    if (bytes == 0u) {
        return;
    }
    const BufferOffset at = resolve(address);
    Buffer* const buffer = at.object;
    if (buffer == nullptr || at.offset >= buffer->size) {
        return;
    }
    const std::uint64_t end = std::min(buffer->size, at.offset + bytes);
    std::lock_guard<std::mutex> lock(dirty_mutex_);
    buffer->writes.fetch_add(1u, std::memory_order_release);
    if (!buffer->copy) {
        return;
    }
    if (buffer->dirty_begin == UINT64_MAX) {
        dirty_.push_back(buffer);
    }
    buffer->dirty_begin = std::min(buffer->dirty_begin, at.offset);
    buffer->dirty_end = std::max(buffer->dirty_end, end);
    dirty_pending_.store(true, std::memory_order_release);
}

void Context::forget_writes(Buffer* buffer) noexcept {
    std::lock_guard<std::mutex> lock(dirty_mutex_);
    dirty_.erase(std::remove(dirty_.begin(), dirty_.end(), buffer), dirty_.end());
}

void Context::flush_dirty() {
    std::lock_guard<std::mutex> lock(dirty_mutex_);
    for (Buffer* buffer : dirty_) {
        if (buffer->dirty_begin < buffer->dirty_end) {
            // From the copy, mapped unsynchronized (glthread::write_buffer): no driver copy of a buffer in use and,
            // with the GL thread, no copy of the bytes in its queue.
            glthread::write_buffer(buffer->name, buffer->dirty_begin, buffer->dirty_end - buffer->dirty_begin,
                                   buffer->copy.get() + buffer->dirty_begin);
            ++flushed_ranges_;
            flushed_bytes_ += buffer->dirty_end - buffer->dirty_begin;
        }
        buffer->dirty_begin = UINT64_MAX;
        buffer->dirty_end = 0;
    }
    dirty_.clear();
    dirty_pending_.store(false, std::memory_order_release);
}

bool Context::check_errors(const char* where) {
    bool clean = true;
    for (int i = 0; i < 8; ++i) {
        const GLenum error = glGetError();
        if (error == GL_NO_ERROR) {
            break;
        }
        clean = false;
        static std::atomic<unsigned> reported{0};
        if (reported.fetch_add(1u, std::memory_order_relaxed) < 64u) {
            std::fprintf(stderr, "[gles] GL error 0x%04x after %s\n", error, where);
        }
    }
    return clean;
}

void apply_bindings(unsigned program, std::string_view bindings) {
    if (bindings.empty() || program == 0u) {
        return;
    }
    GLint previous = 0;
    bool using_program = false;
    for (std::size_t at = 0; at < bindings.size();) {
        const std::size_t end = std::min(bindings.find('\n', at), bindings.size());
        const std::string line(bindings.substr(at, end - at));
        at = end + 1u;
        char kind[16]{};
        char name[256]{};
        unsigned value = 0;
        if (std::sscanf(line.c_str(), "// nebula-bind %15s %255s %u", kind, name, &value) != 3) {
            continue;
        }
        if (std::strcmp(kind, "block") == 0) {
            const GLuint index = glGetUniformBlockIndex(program, name);
            if (index != GL_INVALID_INDEX) {
                glUniformBlockBinding(program, index, value);
            }
        } else if (std::strcmp(kind, "sampler") == 0) {
            const GLint location = glGetUniformLocation(program, name);
            if (location < 0) {
                continue;  // optimised out
            }
            if (!using_program) {
                glGetIntegerv(GL_CURRENT_PROGRAM, &previous);
                glUseProgram(program);
                using_program = true;
            }
            glUniform1i(location, static_cast<GLint>(value));
        }
    }
    if (using_program) {
        glUseProgram(static_cast<GLuint>(previous));
    }
}

std::int64_t thread_cpu_ns() noexcept {
    timespec ts{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) != 0) {
        return 0;
    }
    return static_cast<std::int64_t>(ts.tv_sec) * 1000000000ll + ts.tv_nsec;
}

}  // namespace galaxy::gx::gles

namespace galaxy::gx::gpu {

TextureRef texture_ref(TextureObject* texture) {
    return texture != nullptr ? texture->shared_from_this() : TextureRef{};
}

Resource* raw(const BufferRef& buffer) noexcept {
    return buffer.get();
}

UploadMemory create_upload_memory(Device*, std::size_t bytes, const char* name) {
    gles::Context& context = gles::Context::get();
    if (!context.ready() || bytes == 0u) {
        return {};
    }
    std::shared_ptr<gles::Buffer> buffer = context.create_buffer(bytes, name);
    if (!buffer) {
        return {};
    }
    UploadMemory out;
    out.cpu = buffer->mapped;
    out.gpu = buffer->address;
    out.buffer = std::move(buffer);
    return out;
}

void note_upload_write(Address address, std::size_t bytes) noexcept {
    gles::Context& context = gles::Context::get();
    if (context.tracking_writes()) {
        context.note_write(address, bytes);
    }
}

TextureRef create_render_texture(Device*, std::uint32_t width, std::uint32_t height) {
    return gles::Context::get().create_texture(width, height, 1u, GL_RGBA8, true, "XFB copy");
}

}  // namespace galaxy::gx::gpu
