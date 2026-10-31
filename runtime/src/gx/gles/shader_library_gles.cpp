// SPDX-License-Identifier: GPL-3.0-only
// The GLSL library (shader_library_gles.h).
#include "galaxy/gx/gles/shader_library_gles.h"

#include <zlib.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <list>
#include <mutex>

namespace galaxy::gx::gles {
namespace {

constexpr std::uint32_t kMagic = 0x534c474eu;  // "NGLS"
constexpr std::uint32_t kVersion = 1u;
constexpr std::size_t kBlockTarget = 256u << 10u;  // raw bytes a block holds (one shader may be more)
constexpr std::size_t kCachedBlocks = 4u;

struct Entry {
    std::uint64_t key = 0;
    std::uint32_t block = 0;
    std::uint32_t offset = 0;
    std::uint32_t length = 0;
    std::uint32_t reserved = 0;
};
static_assert(sizeof(Entry) == 24);

std::uint64_t fnv(const void* data, std::size_t size, std::uint64_t hash = 0xCBF29CE484222325ull) noexcept {
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 0x100000001B3ull;
    }
    return hash;
}

template <typename T>
bool read_pod(std::FILE* f, T& value) {
    return std::fread(&value, sizeof(T), 1, f) == 1;
}

class Library {
public:
    bool open(const std::filesystem::path& path, std::string& error) {
        file_ = std::fopen(path.string().c_str(), "rb");
        if (file_ == nullptr) {
            error = "cannot open " + path.string();
            return false;
        }
        std::uint32_t magic = 0, version = 0, entries = 0, blocks = 0;
        if (!read_pod(file_, magic) || !read_pod(file_, version) || !read_pod(file_, entries) ||
            !read_pod(file_, blocks) || magic != kMagic || version != kVersion || entries > (1u << 24) ||
            blocks > (1u << 20)) {
            error = path.string() + " is not a GLSL library of this version";
            return false;
        }
        entries_.resize(entries);
        if (entries != 0u && std::fread(entries_.data(), sizeof(Entry), entries, file_) != entries) {
            error = path.string() + " is cut short";
            return false;
        }
        // The blocks' offsets: each one's header, walked once.
        long at = std::ftell(file_);
        block_offsets_.resize(blocks);
        for (std::uint32_t b = 0; b < blocks; ++b) {
            std::uint32_t packed = 0, raw = 0;
            if (std::fseek(file_, at, SEEK_SET) != 0 || !read_pod(file_, packed) || !read_pod(file_, raw)) {
                error = path.string() + " is cut short";
                return false;
            }
            block_offsets_[b] = at;
            at += 8 + static_cast<long>(packed);
        }
        return std::is_sorted(entries_.begin(), entries_.end(),
                              [](const Entry& a, const Entry& b) { return a.key < b.key; }) ||
               (error = path.string() + " has unsorted entries", false);
    }

    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

    bool find(std::uint64_t key, std::string& out) {
        const auto it = std::lower_bound(entries_.begin(), entries_.end(), key,
                                         [](const Entry& e, std::uint64_t k) { return e.key < k; });
        if (it == entries_.end() || it->key != key) {
            return false;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        const std::string* block = load_block(it->block);
        if (block == nullptr || std::size_t{it->offset} + it->length > block->size()) {
            return false;
        }
        out.assign(*block, it->offset, it->length);
        return true;
    }

    template <typename F>
    bool each(F&& visit) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const Entry& e : entries_) {
            const std::string* block = load_block(e.block);
            if (block == nullptr || std::size_t{e.offset} + e.length > block->size()) return false;
            visit(e.key, std::string(*block, e.offset, e.length));
        }
        return true;
    }

    ~Library() {
        if (file_ != nullptr) std::fclose(file_);
    }

private:
    const std::string* load_block(std::uint32_t index) {
        for (auto it = cache_.begin(); it != cache_.end(); ++it) {
            if (it->first == index) {
                cache_.splice(cache_.begin(), cache_, it);
                return &cache_.front().second;
            }
        }
        if (index >= block_offsets_.size() || std::fseek(file_, block_offsets_[index], SEEK_SET) != 0) {
            return nullptr;
        }
        std::uint32_t packed = 0, raw = 0;
        if (!read_pod(file_, packed) || !read_pod(file_, raw) || raw > (64u << 20u) || packed > (64u << 20u)) {
            return nullptr;
        }
        std::vector<unsigned char> in(packed);
        if (packed != 0u && std::fread(in.data(), 1, packed, file_) != packed) {
            return nullptr;
        }
        std::string text(raw, '\0');
        uLongf length = raw;
        if (uncompress(reinterpret_cast<Bytef*>(text.data()), &length, in.data(), packed) != Z_OK || length != raw) {
            return nullptr;
        }
        cache_.emplace_front(index, std::move(text));
        if (cache_.size() > kCachedBlocks) {
            cache_.pop_back();
        }
        return &cache_.front().second;
    }

    std::FILE* file_ = nullptr;
    std::vector<Entry> entries_;
    std::vector<long> block_offsets_;
    std::mutex mutex_;
    std::list<std::pair<std::uint32_t, std::string>> cache_;
};

std::atomic<std::uint64_t> g_hits{0}, g_misses{0}, g_translations{0};

Library* process_library() {
    static Library* const library = []() -> Library* {
        const char* path = std::getenv("GALAXY_GLES_SHADER_LIBRARY");
        if (path == nullptr || path[0] == '\0') {
            if (!translator_available()) {
                std::fprintf(stderr, "[glsl-library] no GALAXY_GLES_SHADER_LIBRARY: this build has no translator, "
                                     "so no shader can be built\n");
            }
            return nullptr;
        }
        auto* lib = new Library();
        std::string error;
        if (!lib->open(path, error)) {
            std::fprintf(stderr, "[glsl-library] %s\n", error.c_str());
            delete lib;
            return nullptr;
        }
        std::fprintf(stderr, "[glsl-library] %zu shaders in %s%s\n", lib->size(), path,
                     translator_available() ? "" : " (no translator in this build)");
        return lib;
    }();
    return library;
}

}  // namespace

std::uint64_t library_key(std::string_view hlsl, Stage stage, ShaderKind kind,
                          const TranslateOptions& options) noexcept {
    std::uint64_t hash = fnv(hlsl.data(), hlsl.size());
    const std::uint32_t form[4] = {
        stage == Stage::Vertex ? 1u : 2u,
        kind == ShaderKind::Gx ? 1u : 2u,
        static_cast<std::uint32_t>(options.glsl_version),
        kind == ShaderKind::Gx ? (options.palette_storage_block ? 1u : 2u) : 0u,
    };
    return fnv(form, sizeof(form), hash);
}

bool translator_available() noexcept {
#if defined(NEBULA_GLES_NO_TRANSLATOR)
    return false;
#else
    return true;
#endif
}

int glsl_version_for(int major, int minor) noexcept {
    if (const char* force = std::getenv("GALAXY_GLES_GLSL")) {
        if (std::strcmp(force, "310") == 0) return 310;
        if (std::strcmp(force, "320") == 0) return 320;
    }
    return major > 3 || (major == 3 && minor >= 2) ? 320 : 310;
}

#if defined(NEBULA_GLES_NO_TRANSLATOR)
std::string translate(std::string_view, Stage, ShaderKind, const TranslateOptions&, std::string& error) {
    error = "this build has no HLSL translator (NEBULA_GLES_TRANSLATOR=OFF)";
    return {};
}

std::string prepare_hlsl(std::string_view hlsl, ShaderKind, const TranslateOptions&) {
    return std::string(hlsl);
}
#endif

std::string shader_glsl(std::string_view hlsl, Stage stage, ShaderKind kind, const TranslateOptions& options,
                        std::string& error) {
    if (Library* library = process_library()) {
        std::string glsl;
        if (library->find(library_key(hlsl, stage, kind, options), glsl)) {
            g_hits.fetch_add(1u, std::memory_order_relaxed);
            return glsl;
        }
    }
    g_misses.fetch_add(1u, std::memory_order_relaxed);
    if (!translator_available()) {
        error = "not in the GLSL library (and this build has no translator)";
        return {};
    }
    g_translations.fetch_add(1u, std::memory_order_relaxed);
    return translate(hlsl, stage, kind, options, error);
}

LibraryStats library_stats() noexcept {
    LibraryStats s;
    Library* library = process_library();
    s.open = library != nullptr;
    s.entries = library != nullptr ? library->size() : 0u;
    s.hits = g_hits.load(std::memory_order_relaxed);
    s.misses = g_misses.load(std::memory_order_relaxed);
    s.translations = g_translations.load(std::memory_order_relaxed);
    return s;
}

bool write_shader_library(const std::filesystem::path& file, std::vector<std::pair<std::uint64_t, std::string>> entries,
                          std::string& error) {
    std::vector<Entry> index;
    std::vector<std::string> blocks(1);
    for (auto& [key, glsl] : entries) {
        if (!blocks.back().empty() && blocks.back().size() + glsl.size() > kBlockTarget) {
            blocks.emplace_back();
        }
        Entry e;
        e.key = key;
        e.block = static_cast<std::uint32_t>(blocks.size() - 1u);
        e.offset = static_cast<std::uint32_t>(blocks.back().size());
        e.length = static_cast<std::uint32_t>(glsl.size());
        blocks.back() += glsl;
        index.push_back(e);
    }
    std::sort(index.begin(), index.end(), [](const Entry& a, const Entry& b) { return a.key < b.key; });
    for (std::size_t i = 1; i < index.size(); ++i) {
        if (index[i].key == index[i - 1].key) {
            error = "two entries with one key";
            return false;
        }
    }
    const std::filesystem::path partial = file.string() + ".part";
    std::FILE* f = std::fopen(partial.string().c_str(), "wb");
    if (f == nullptr) {
        error = "cannot write " + partial.string();
        return false;
    }
    bool ok = true;
    const auto put = [&](const void* data, std::size_t size) {
        ok = ok && (size == 0u || std::fwrite(data, 1, size, f) == size);
    };
    const std::uint32_t header[4] = {kMagic, kVersion, static_cast<std::uint32_t>(index.size()),
                                     static_cast<std::uint32_t>(blocks.size())};
    put(header, sizeof(header));
    put(index.data(), index.size() * sizeof(Entry));
    for (const std::string& block : blocks) {
        uLongf packed = compressBound(block.size());
        std::vector<unsigned char> out(packed);
        if (compress2(out.data(), &packed, reinterpret_cast<const Bytef*>(block.data()), block.size(), 9) != Z_OK) {
            ok = false;
            break;
        }
        const std::uint32_t sizes[2] = {static_cast<std::uint32_t>(packed), static_cast<std::uint32_t>(block.size())};
        put(sizes, sizeof(sizes));
        put(out.data(), packed);
    }
    ok = (std::fclose(f) == 0) && ok;
    std::error_code ec;
    if (ok) {
        std::filesystem::rename(partial, file, ec);
        ok = !ec;
    }
    if (!ok) {
        std::filesystem::remove(partial, ec);
        error = "cannot write " + file.string();
    }
    return ok;
}

bool read_shader_library(const std::filesystem::path& file, std::vector<std::pair<std::uint64_t, std::string>>& out,
                         std::string& error) {
    Library library;
    if (!library.open(file, error)) {
        return false;
    }
    out.clear();
    if (!library.each([&](std::uint64_t key, std::string glsl) { out.emplace_back(key, std::move(glsl)); })) {
        error = file.string() + ": a block does not decompress";
        return false;
    }
    return true;
}

}  // namespace galaxy::gx::gles
