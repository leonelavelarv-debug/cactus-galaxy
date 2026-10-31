// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The per-frame upload rings both GX renderers allocate draw data from (vertices, indices, constants, matrix
// palettes), over upload memory from gpu::create_upload_memory.

#include "galaxy/gx/gpu_types.h"
#include "galaxy/gx/immutable_upload.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace galaxy::gx {

// Linear per-frame allocator over one persistently mapped upload buffer,
// partitioned into kFramesInFlight segments so frame N never overwrites
// memory the GPU is still reading for frame N-1.
class UploadRing {
public:
    // Caller must have retired all GPU references to this resource.
    void reset() noexcept;
    struct Allocation {
        std::byte* cpu = nullptr;
        gpu::Address gpu = 0;
        std::size_t offset = 0;     // within the whole resource
    };

    // `capacity`: bytes reserved beyond bytes_per_frame * frames_in_flight, so later
    // resegment() calls up to it need no new buffer.
    bool initialize(
        gpu::Device* device,
        const char* name,
        std::size_t bytes_per_frame,
        unsigned frames_in_flight,
        std::size_t capacity = 0);
    // A new segmentation inside the existing buffer (the caller has retired every GPU
    // reference to its contents); false when it does not fit. Immutable uploads start over.
    bool resegment(std::size_t bytes_per_frame, unsigned frames_in_flight);
    void begin_frame(unsigned frame_slot);

    // Bump-allocates within the current frame segment. Exhaustion is a GxFatalError:
    // geometry is never dropped silently. With an overflow handler (set_overflow), a frame that
    // fills its segment first continues in the next slot's segment, if the handler frees it.
    // Alignment must be a nonzero power of two. Rejected requests do not
    // consume bytes or advance the segment's content generation.
    Allocation allocate(std::size_t size, std::size_t alignment);

    // `borrow(slot)`: the renderer waits until the GPU is done with `slot`'s last frame and keeps that
    // slot for the current frame (the next frame to use it waits for this one); false refuses. Up to
    // frames_in_flight - 1 slots a frame. Without a handler (D3D12, Vulkan) exhaustion stays fatal.
    void set_overflow(std::function<bool(unsigned slot)> borrow) { borrow_ = std::move(borrow); }
    // Whether `size` bytes at `alignment` fit in what is left of the current segment (no slot change). A caller
    // that needs its next allocations contiguous with the last ones (GxBackend's draw batches) asks first.
    [[nodiscard]] bool fits(std::size_t size, std::size_t alignment) const noexcept {
        const std::size_t padding = (std::size_t{0} - cursor_) & (alignment - 1u);
        const std::size_t remaining = bytes_per_frame_ - (cursor_ - segment_base_);
        return padding <= remaining && size <= remaining - padding;
    }
    // Slots this frame continued into, and how many times that has happened (diagnostics).
    [[nodiscard]] unsigned borrowed_slots() const { return borrowed_; }
    [[nodiscard]] std::uint64_t overflows() const { return overflows_; }

    Allocation upload_immutable(
        const void* source, std::size_t size, std::size_t alignment,
        ImmutableUploadToken& token);
    [[nodiscard]] std::uint64_t reused_bytes() const { return reused_bytes_; }
    [[nodiscard]] std::uint64_t copied_bytes() const { return copied_bytes_; }
    // Number of upload_immutable() calls this frame. reused_bytes() and copied_bytes()
    // are only meaningful relative to this count: both are zero for a frame in which
    // the immutable path was never entered, which looks the same on the timing line as
    // a frame in which it was entered and classified every byte as a copy. The call
    // count is the field that separates "the reuse path is not reached" from "the
    // counters are not observed". The increment costs one add on a path that already
    // does a memcpy or a map lookup.
    [[nodiscard]] std::uint64_t immutable_upload_calls() const {
        return immutable_upload_calls_;
    }

    // Peak bytes used within a single frame segment, and the segment size. These show
    // whether the ring's configured size is resident memory or only reserved address
    // space. The rings are upload buffers (D3D12_HEAP_TYPE_UPLOAD, host-visible Vulkan memory) mapped for the life
    // of the process, so the reservation is bytes_per_frame * frames_in_flight (240 MiB
    // by default). Physical pages are committed only for the parts that were written,
    // so the physical cost tracks the peak cursor rather than the reservation. Exhaustion
    // is fatal, so the floor for any reduction is peak_bytes_per_frame().
    [[nodiscard]] std::size_t peak_bytes_per_frame() const {
        return peak_bytes_per_frame_;
    }
    // The peak since the last call (diagnostics windows).
    [[nodiscard]] std::size_t take_peak_bytes_per_frame() noexcept {
        const std::size_t peak = peak_bytes_per_frame_;
        peak_bytes_per_frame_ = 0u;
        return peak;
    }
    [[nodiscard]] std::size_t bytes_per_frame() const { return bytes_per_frame_; }
    [[nodiscard]] std::size_t total_bytes() const { return bytes_per_frame_ * frames_in_flight_; }
    // CPU view of a GPU address inside this ring; null outside it.
    [[nodiscard]] const std::byte* cpu_at(gpu::Address va) const {
        return va >= gpu_base_ && va < gpu_base_ + total_bytes() ? mapped_ + (va - gpu_base_) : nullptr;
    }
    [[nodiscard]] std::size_t cursor() const { return cursor_; }

    [[nodiscard]] gpu::Resource* resource() const { return gpu::raw(buffer_); }
    [[nodiscard]] gpu::Address gpu_base() const { return gpu_base_; }
    // The persistent mapping (the whole resource, all segments).
    [[nodiscard]] std::byte* mapped() const { return mapped_; }

private:
    gpu::BufferRef buffer_;
    const char* name_ = "";
    std::byte* mapped_ = nullptr;
    // This committed buffer keeps its address until it is replaced by initialize.
    gpu::Address gpu_base_ = 0;
    std::size_t bytes_per_frame_ = 0;
    std::size_t capacity_ = 0;
    std::size_t segment_base_ = 0;
    std::size_t cursor_ = 0;
    unsigned frames_in_flight_ = 0;
    unsigned active_slot_ = 0;
    bool frame_ready_ = false;
    bool segment_allocation_pending_ = true;
    std::uint64_t resource_identity_ = 0;
    std::vector<std::uint64_t> slot_generations_;
    std::uint64_t reused_bytes_ = 0;
    std::uint64_t copied_bytes_ = 0;
    std::uint64_t immutable_upload_calls_ = 0;
    std::size_t peak_bytes_per_frame_ = 0;
    std::function<bool(unsigned)> borrow_;
    unsigned borrowed_ = 0;            // slots after active_slot_ this frame continued into
    std::size_t frame_used_before_ = 0;  // bytes the frame used in the segments before the current one
    std::uint64_t overflows_ = 0;
    // Moves the frame into the next free slot (see set_overflow); false when there is none.
    bool continue_in_next_slot(std::size_t size, std::size_t alignment);
};

// A mapped upload buffer that lives until it is released (GxBackend's resident decoded vertices).
using UploadBuffer = gpu::UploadMemory;

}  // namespace galaxy::gx
