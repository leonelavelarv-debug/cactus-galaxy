// SPDX-License-Identifier: GPL-3.0-only
// The display-list cache's byte check (FifoParser::advance_memory_generation): a cached list called again before the
// next batch of guest writes replays without comparing its bytes; after the batch, a changed list is parsed again
// and an unchanged one compares once. A parser never advanced compares every call, as before.
#include "galaxy/gx/fifo_parser.h"
#include "galaxy/native_api.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {
using galaxy::gx::FifoCursor;
using galaxy::gx::FifoParser;
using galaxy::gx::FifoSink;
using galaxy::gx::GxState;
using galaxy::gx::PrimitiveClass;
using galaxy::GuestMemoryRegionV1;
using galaxy::GuestMemoryV1;

int failures = 0;
void expect(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FALLA: %s\n", what);
        ++failures;
    }
}

constexpr std::size_t kVertexBytes = 4;

struct Sink final : FifoSink {
    std::vector<std::uint32_t> first_words;  // each draw's first vertex word: which list bytes were drawn
    [[nodiscard]] std::size_t draw_payload_size(std::uint8_t, std::uint16_t count) const override {
        return count * kVertexBytes;
    }
    void on_draw(PrimitiveClass, std::uint8_t, FifoCursor& cursor) override {
        const std::uint16_t count = cursor.read_u16();
        const auto payload = cursor.take(count * kVertexBytes);
        first_words.push_back(static_cast<std::uint32_t>(payload[0]) << 24 |
                              static_cast<std::uint32_t>(payload[1]) << 16 |
                              static_cast<std::uint32_t>(payload[2]) << 8 | static_cast<std::uint32_t>(payload[3]));
    }
    void on_efb_copy(std::uint32_t) override {}
    void on_pe_finish() override {}
    void on_pe_token(std::uint16_t, bool) override {}
    void on_invalidate_textures() override {}
    void on_invalidate_vertex_cache() override {}
};

constexpr std::uint32_t kListAddr = 0x00001000u;
constexpr std::uint32_t kListSize = 64u;

// The list: one triangle strip of 3 vertices whose first word is `tag`, padded with NOPs to 64 bytes.
void write_list(std::vector<std::byte>& ram, std::uint32_t tag) {
    std::byte* p = ram.data() + kListAddr;
    std::size_t i = 0;
    p[i++] = std::byte{0x98};
    p[i++] = std::byte{0};
    p[i++] = std::byte{3};
    for (unsigned v = 0; v < 3; ++v) {
        const std::uint32_t word = v == 0 ? tag : 0x11111111u * (v + 1);
        p[i++] = std::byte(word >> 24);
        p[i++] = std::byte(word >> 16);
        p[i++] = std::byte(word >> 8);
        p[i++] = std::byte(word);
    }
    while (i < kListSize) {
        p[i++] = std::byte{0x00};
    }
}

// A frame's FIFO: the list called `calls` times, padded to 32 bytes.
std::vector<std::byte> fifo(unsigned calls) {
    std::vector<std::byte> f;
    for (unsigned c = 0; c < calls; ++c) {
        f.push_back(std::byte{0x40});
        for (const std::uint32_t word : {kListAddr, kListSize}) {
            f.push_back(std::byte(word >> 24));
            f.push_back(std::byte(word >> 16));
            f.push_back(std::byte(word >> 8));
            f.push_back(std::byte(word));
        }
    }
    while (f.size() % 32u != 0u) {
        f.push_back(std::byte{0x00});
    }
    return f;
}

std::vector<std::uint32_t> run(FifoParser& parser, GuestMemoryV1& memory, GxState& state, unsigned calls,
                               std::vector<std::byte>* ram = nullptr, std::uint32_t change_after_first = 0) {
    Sink sink;
    const auto f = fifo(1);
    for (unsigned c = 0; c < calls; ++c) {
        parser.run(f, &memory, sink, state);
        if (c == 0 && ram != nullptr) {
            write_list(*ram, change_after_first);  // a write inside the frame, before the next batch
        }
    }
    return sink.first_words;
}
}  // namespace

int main() {
    std::vector<std::byte> ram(0x10000);
    GuestMemoryRegionV1 region{0u, static_cast<std::uint32_t>(ram.size()), ram.data()};
    GuestMemoryV1 memory{};
    memory.region_count = 1;
    memory.regions = &region;

    // VAT 0 with no attributes is enough: the sink takes 4 bytes a vertex.
    {
        FifoParser parser;
        parser.set_dump_dir("/tmp");
        GxState state;
        write_list(ram, 0xAAAA0001u);
        parser.advance_memory_generation();
        auto words = run(parser, memory, state, 3);
        expect(words.size() == 3 && words[0] == 0xAAAA0001u && words[2] == 0xAAAA0001u, "primer frame");

        // The list changes, then the next batch: the change is seen on the first call.
        write_list(ram, 0xBBBB0002u);
        parser.advance_memory_generation();
        words = run(parser, memory, state, 2);
        expect(words.size() == 2 && words[0] == 0xBBBB0002u && words[1] == 0xBBBB0002u, "cambio tras el lote");

        // Unchanged across a batch: still the same.
        parser.advance_memory_generation();
        words = run(parser, memory, state, 2);
        expect(words.size() == 2 && words[0] == 0xBBBB0002u, "sin cambio tras el lote");

        // A write inside a generation (no batch in between) is not looked for: the rule of the decoded vertex cache.
        parser.advance_memory_generation();
        words = run(parser, memory, state, 2, &ram, 0xCCCC0003u);
        expect(words.size() == 2 && words[1] == 0xBBBB0002u, "dentro del mismo lote se reusa");
        parser.advance_memory_generation();
        words = run(parser, memory, state, 1);
        expect(words.size() == 1 && words[0] == 0xCCCC0003u, "y el lote siguiente lo ve");
    }
    {
        // Never advanced: every call compares (the event classifier and the dependency scanner).
        FifoParser parser;
        parser.set_dump_dir("/tmp");
        GxState state;
        write_list(ram, 0xAAAA0001u);
        auto words = run(parser, memory, state, 2, &ram, 0xDDDD0004u);
        expect(words.size() == 2 && words[0] == 0xAAAA0001u && words[1] == 0xDDDD0004u, "sin generacion compara siempre");
    }
    if (failures == 0) {
        std::printf("fifo_display_list_tests: ok\n");
    }
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
