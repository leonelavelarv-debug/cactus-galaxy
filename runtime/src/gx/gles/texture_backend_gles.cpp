// SPDX-License-Identifier: GPL-3.0-only
// TextureCache's OpenGL ES side (texture_backend.h).

#include "galaxy/gx/texture_backend.h"
#include "galaxy/gx/gles/ultra_gles.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <stdexcept>

namespace galaxy::gx {

using namespace gles::gl;

bool TextureBackend::initialize(gpu::Device*, std::uint32_t view_capacity) {
    // The null view: one transparent black texel, sampled where a draw binds no map.
    null_texture_ = gles::Context::get().create_texture(1u, 1u, 1u, GL_RGBA8, false, "null texture");
    if (!null_texture_) {
        return false;
    }
    const std::uint8_t black[4] = {0, 0, 0, 0};
    glBindTexture(GL_TEXTURE_2D, null_texture_->name);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, black);
    glBindTexture(GL_TEXTURE_2D, 0);
    views_.assign(view_capacity + 1u, 0u);
    views_.back() = null_texture_->name;
    return true;
}

void TextureBackend::shutdown() {
    views_.clear();
    null_texture_.reset();
    upload_list_ = false;
}

void TextureBackend::write_view(std::uint32_t index, gles::Texture* texture, unsigned) {
    views_[index] = texture != nullptr ? texture->name : views_.back();
}

TextureBackend::UploadChunk TextureBackend::create_upload_chunk(std::uint64_t size) {
    UploadChunk chunk{};
    chunk.resource = std::shared_ptr<std::uint8_t[]>(new (std::nothrow) std::uint8_t[size]);
    if (!chunk.resource) {
        throw std::runtime_error("[TextureCache] texture upload arena allocation failed");
    }
    chunk.cpu_base = chunk.resource.get();
    chunk.size = size;
    return chunk;
}

void TextureBackend::release_upload_chunk(UploadChunk& chunk) noexcept {
    chunk.cpu_base = nullptr;
    chunk.resource.reset();
}

namespace {

GLenum internal_format(CompactFormat format) noexcept {
    switch (format) {
    case CompactFormat::R8: return GL_R8;
    case CompactFormat::RG8: return GL_RG8;
    case CompactFormat::RGB565: return GL_RGB565;
    case CompactFormat::RGB5A1: return GL_RGB5_A1;
    case CompactFormat::RGBA4: return GL_RGBA4;
    default: return GL_RGBA8;
    }
}

// The client format and type glTexSubImage2D reads a level of `format` in.
void upload_format(CompactFormat format, GLenum& client, GLenum& type) noexcept {
    switch (format) {
    case CompactFormat::R8: client = GL_RED; type = GL_UNSIGNED_BYTE; break;
    case CompactFormat::RG8: client = GL_RG; type = GL_UNSIGNED_BYTE; break;
    case CompactFormat::RGB565: client = GL_RGB; type = GL_UNSIGNED_SHORT_5_6_5; break;
    case CompactFormat::RGB5A1: client = GL_RGBA; type = GL_UNSIGNED_SHORT_5_5_5_1; break;
    case CompactFormat::RGBA4: client = GL_RGBA; type = GL_UNSIGNED_SHORT_4_4_4_4; break;
    default: client = GL_RGBA; type = GL_UNSIGNED_BYTE; break;
    }
}

// Compact textures uploaded (and the bytes RGBA8 would have taken more): a log line at 1, 64, 256, then every 1024.
struct CompactCount {
    std::array<std::uint64_t, 6> by_format{};
    std::uint64_t textures = 0;
    std::uint64_t saved = 0;
} g_compact;

void count_compact(CompactFormat format, std::uint64_t texels) {
    ++g_compact.by_format[static_cast<unsigned>(format)];
    ++g_compact.textures;
    g_compact.saved += texels * (4u - compact_texel_bytes(format));
    const std::uint64_t n = g_compact.textures;
    if (n == 1u || n == 64u || n == 256u || n % 1024u == 0u) {
        const auto& f = g_compact.by_format;
        std::fprintf(stderr,
                     "[texture-compact] %llu textures (R8 %llu, RG8 %llu, RGB565 %llu, RGB5A1 %llu, RGBA4 %llu), "
                     "%.1f MiB less than RGBA8\n",
                     static_cast<unsigned long long>(n), static_cast<unsigned long long>(f[1]),
                     static_cast<unsigned long long>(f[2]), static_cast<unsigned long long>(f[3]),
                     static_cast<unsigned long long>(f[4]), static_cast<unsigned long long>(f[5]),
                     static_cast<double>(g_compact.saved) / (1024.0 * 1024.0));
    }
}

void count_upload(std::uint64_t bytes) {
    if (ultra::enabled()) {
        ultra::add(ultra::kTextureUploads, 1u);
        ultra::add(ultra::kTextureUploadBytes, bytes);
    }
}

}  // namespace

CompactFormat TextureBackend::compact_format(const gles::Texture* texture) noexcept {
    if (texture == nullptr) return CompactFormat::RGBA8;
    switch (texture->format) {
    case GL_R8: return CompactFormat::R8;
    case GL_RG8: return CompactFormat::RG8;
    case GL_RGB565: return CompactFormat::RGB565;
    case GL_RGB5_A1: return CompactFormat::RGB5A1;
    case GL_RGBA4: return CompactFormat::RGBA4;
    default: return CompactFormat::RGBA8;
    }
}

gpu::TextureRef TextureBackend::create_texture(
    std::uint32_t width, std::uint32_t height, unsigned levels, std::uint64_t& allocation_bytes) {
    return create_texture(width, height, levels, allocation_bytes, CompactFormat::RGBA8);
}

gpu::TextureRef TextureBackend::create_texture(std::uint32_t width, std::uint32_t height, unsigned levels,
                                               std::uint64_t& allocation_bytes, CompactFormat format) {
    auto texture = gles::Context::get().create_texture(width, height, levels, internal_format(format), false, nullptr);
    if (!texture) {
        throw std::runtime_error("[TextureCache] create texture failed");
    }
    allocation_bytes = texture->allocation_bytes;
    return texture;
}

bool TextureBackend::poolable(gles::Texture* texture, std::uint32_t& width, std::uint32_t& height,
                              std::uint32_t& levels) {
    if (texture->format != GL_RGBA8 || texture->render_target) {
        return false;
    }
    width = texture->width;
    height = texture->height;
    levels = texture->levels;
    return true;
}

bool TextureBackend::alias_compatible(gles::Texture* texture, std::uint32_t& width, std::uint32_t& height) {
    if (texture->format != GL_RGBA8 || texture->width == 0u || texture->width > UINT16_MAX ||
        texture->height == 0u || texture->height > UINT16_MAX) {
        return false;
    }
    width = texture->width;
    height = texture->height;
    return true;
}

void TextureBackend::record_upload(gles::Texture* texture, bool reused, const UploadChunk& staging, std::uint64_t offset,
                                   std::uint32_t width, std::uint32_t height, unsigned levels,
                                   const std::uint64_t* offsets, const std::uint32_t* pitches) {
    if (texture == nullptr || staging.cpu_base == nullptr) {
        return;
    }
    const CompactFormat compact = compact_format(texture);
    glBindTexture(GL_TEXTURE_2D, texture->name);
    const unsigned count = std::min(levels, texture->levels);
    const std::uint64_t before = uploaded_bytes_;
    if (compact != CompactFormat::RGBA8) {
        // Each level converted here, tightly packed (the GL thread copies it at the call, so one buffer serves all).
        GLenum client = GL_RGBA, type = GL_UNSIGNED_BYTE;
        upload_format(compact, client, type);
        const std::uint32_t texel = compact_texel_bytes(compact);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        for (unsigned l = 0; l < count; ++l) {
            const std::uint32_t lw = std::max<std::uint32_t>(1u, width >> l);
            const std::uint32_t lh = std::max<std::uint32_t>(1u, height >> l);
            compact_rows_.resize(static_cast<std::size_t>(lw) * lh * texel);
            for (std::uint32_t row = 0; row < lh; ++row) {
                compact_convert(compact, staging.cpu_base + offset + offsets[l] + static_cast<std::uint64_t>(row) * pitches[l],
                                lw, compact_rows_.data() + static_cast<std::size_t>(row) * lw * texel);
            }
            glTexSubImage2D(GL_TEXTURE_2D, static_cast<GLint>(l), 0, 0, static_cast<GLsizei>(lw),
                            static_cast<GLsizei>(lh), client, type, compact_rows_.data());
            uploaded_bytes_ += static_cast<std::uint64_t>(lw) * lh * texel;
        }
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glBindTexture(GL_TEXTURE_2D, 0);
        count_upload(uploaded_bytes_ - before);
        if (!reused) count_compact(compact, (uploaded_bytes_ - before) / texel);
        return;
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    for (unsigned l = 0; l < count; ++l) {
        const std::uint32_t lw = std::max<std::uint32_t>(1u, width >> l);
        const std::uint32_t lh = std::max<std::uint32_t>(1u, height >> l);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(pitches[l] / 4u));
        glTexSubImage2D(GL_TEXTURE_2D, static_cast<GLint>(l), 0, 0, static_cast<GLsizei>(lw),
                        static_cast<GLsizei>(lh), GL_RGBA, GL_UNSIGNED_BYTE, staging.cpu_base + offset + offsets[l]);
        uploaded_bytes_ += static_cast<std::uint64_t>(lw) * lh * 4u;
    }
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    count_upload(uploaded_bytes_ - before);
}

void TextureBackend::record_upload_compact(gles::Texture* texture, const UploadChunk& staging, std::uint64_t offset,
                                           std::uint32_t width, std::uint32_t height, unsigned levels,
                                           const std::uint64_t* offsets, const std::uint32_t* pitches) {
    if (texture == nullptr || staging.cpu_base == nullptr) {
        return;
    }
    const CompactFormat compact = compact_format(texture);
    GLenum client = GL_RGBA, type = GL_UNSIGNED_BYTE;
    upload_format(compact, client, type);
    const std::uint32_t texel = compact_texel_bytes(compact);
    glBindTexture(GL_TEXTURE_2D, texture->name);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    const unsigned count = std::min(levels, texture->levels);
    const std::uint64_t before = uploaded_bytes_;
    for (unsigned l = 0; l < count; ++l) {
        const std::uint32_t lw = std::max<std::uint32_t>(1u, width >> l);
        const std::uint32_t lh = std::max<std::uint32_t>(1u, height >> l);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(pitches[l] / texel));
        glTexSubImage2D(GL_TEXTURE_2D, static_cast<GLint>(l), 0, 0, static_cast<GLsizei>(lw),
                        static_cast<GLsizei>(lh), client, type, staging.cpu_base + offset + offsets[l]);
        uploaded_bytes_ += static_cast<std::uint64_t>(lw) * lh * texel;
    }
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glBindTexture(GL_TEXTURE_2D, 0);
    count_upload(uploaded_bytes_ - before);
    count_compact(compact, (uploaded_bytes_ - before) / texel);
}

}  // namespace galaxy::gx
