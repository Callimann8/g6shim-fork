// SPDX-FileCopyrightText: 2026 gookie and g6shim contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "check.hpp"

#include "apu_memory.hpp"
#include "processor.hpp"
#include "shape.hpp"
#include "title_memory.hpp"

#include <cstring>
#include <vector>

using namespace g6shim::acphal;

TEST(acphal_block_set_bounds) {
    BlockSet set;
    set.insert(0x1000, 0x100);
    CHECK(set.contains(0x1000, 0x100));
    CHECK(set.contains(0x10ff, 1));
    CHECK(!set.contains(0x10ff, 2));
    CHECK(!set.contains(0x0fff, 1));
    CHECK(!set.contains(0x1100, 1));
    CHECK(!set.contains(0x1080, ~std::size_t(0)));
    CHECK(set.erase(0x1000));
    CHECK(!set.erase(0x1000));
    CHECK(!set.contains(0x1000, 1));
}

TEST(acphal_blocks_live_below_4gib) {
    void* p = reserve_below_4gib(0x3000);
    CHECK(p != nullptr);
    CHECK(reinterpret_cast<std::uintptr_t>(p) + 0x3000 <= kApuAddressLimit);
    std::memset(p, 0xab, 0x3000);
    release_block(p);
    CHECK(reserve_below_4gib(0) == nullptr);
    CHECK(reserve_below_4gib(kMaxApuBlock + 1) == nullptr);
}

TEST(acphal_shapes_allocate_and_release) {
    ShapeRequest request{};
    request.count[static_cast<std::size_t>(ShapeKind::Src)] = 512;
    request.count[static_cast<std::size_t>(ShapeKind::Dma)] = 1024;
    request.count[static_cast<std::size_t>(ShapeKind::Xma)] = 512;
    CHECK(allocate_shapes(request) == S_OK);
    for (std::size_t k = 0; k < kShapeKinds; ++k) {
        CHECK((request.array[k] != nullptr) == (request.count[k] != 0));
        if (request.array[k]) {
            CHECK(reinterpret_cast<std::uintptr_t>(request.array[k]) < kApuAddressLimit);
            std::memset(request.array[k], 0, std::size_t(request.count[k]) * kShapeStride[k]);
        }
    }
    ShapeRequest again = request;
    CHECK(allocate_shapes(again) == E_UNEXPECTED);
    CHECK(current_shapes().array[static_cast<std::size_t>(ShapeKind::Xma)] ==
          request.array[static_cast<std::size_t>(ShapeKind::Xma)]);
    CHECK(release_shapes() == S_OK);
    CHECK(release_shapes() == E_UNEXPECTED);
}

TEST(acphal_shapes_reject_huge_counts) {
    ShapeRequest request{};
    request.count[0] = kMaxShapeCount + 1;
    CHECK(allocate_shapes(request) == E_INVALIDARG);
    CHECK(request.array[0] == nullptr);
    CHECK(release_shapes() == E_UNEXPECTED);
}

namespace {

struct Harness {
    std::vector<SubmitBufferPayload> played;
    Processor processor{[this](const SubmitBufferPayload& p) { played.push_back(p); },
                        [](std::uint32_t apu) { return apu >= 0x1000 && apu < 0x2000; }};
};

std::vector<std::uint32_t> drain_types(Processor& p, std::vector<std::uint64_t>* values = nullptr) {
    std::vector<std::uint32_t> types;
    Message m{};
    while (p.next_message(m)) {
        types.push_back(m.type);
        if (values) values->push_back(m.value);
    }
    return types;
}

constexpr std::uint32_t kDone = static_cast<std::uint32_t>(MessageType::BufferDone);
constexpr std::uint32_t kFence = static_cast<std::uint32_t>(MessageType::Fence);
constexpr std::uint32_t kShutdown = static_cast<std::uint32_t>(MessageType::ShutdownDone);

}  // namespace

TEST(acphal_bounded_queue_order_and_capacity) {
    BoundedQueue<int> q(2);
    CHECK(q.push(1) && q.push(2) && !q.push(3));
    CHECK(q.pop() == 1);
    CHECK(q.push(3));
    CHECK(q.pop() == 2 && q.pop() == 3 && !q.pop());
}

TEST(acphal_processor_requires_init) {
    Harness h;
    CHECK(h.processor.submit_buffer({1, 0x1000, 0}) == E_UNEXPECTED);
    CHECK(h.processor.request_shutdown() == E_UNEXPECTED);
    CHECK(h.processor.init(0, 4) == E_INVALIDARG);
    CHECK(h.processor.init(4, kMaxQueueEntries + 1) == E_INVALIDARG);
    CHECK(h.processor.init(0x400, 0x400) == S_OK);
    CHECK(h.processor.init(0x400, 0x400) == E_UNEXPECTED);
}

TEST(acphal_processor_completes_buffers_on_frames) {
    Harness h;
    CHECK(h.processor.init(8, 64) == S_OK);
    for (int i = 0; i < 4; ++i) CHECK(h.processor.submit_buffer({256, 0x1000, 0}) == S_OK);
    CHECK(h.played.empty());
    CHECK(h.processor.pending_messages() == 0);
    h.processor.run_frame();
    CHECK(h.played.size() == 4);
    std::vector<std::uint64_t> values;
    const auto types = drain_types(h.processor, &values);
    CHECK((types == std::vector<std::uint32_t>{kDone, kDone, kDone, kDone, kFence}));
    CHECK(values.back() == 1);
}

TEST(acphal_processor_fences_count_up_by_one) {
    Harness h;
    CHECK(h.processor.init(8, 64) == S_OK);
    std::vector<std::uint64_t> values;
    for (int i = 0; i < 10; ++i) h.processor.run_frame();
    drain_types(h.processor, &values);
    CHECK(values.size() == 10);
    for (std::size_t i = 0; i < values.size(); ++i) CHECK(values[i] == i + 1);
}

TEST(acphal_processor_rejects_foreign_buffers) {
    Harness h;
    CHECK(h.processor.init(2, 8) == S_OK);
    CHECK(h.processor.submit_buffer({256, 0x5000, 0}) == E_INVALIDARG);
    CHECK(h.processor.submit_buffer({256, 0x1000, 0}) == S_OK);
    CHECK(h.processor.submit_buffer({256, 0x1000, 0}) == S_OK);
    CHECK(h.processor.submit_buffer({256, 0x1000, 0}) == E_OUTOFMEMORY);
}

TEST(acphal_processor_shutdown_flushes_then_reports) {
    Harness h;
    CHECK(h.processor.init(8, 64) == S_OK);
    CHECK(h.processor.submit_buffer({256, 0x1000, 0}) == S_OK);
    CHECK(h.processor.request_shutdown() == S_OK);
    const auto types = drain_types(h.processor);
    CHECK((types == std::vector<std::uint32_t>{kDone, kFence, kShutdown}));
}

TEST(acphal_processor_full_message_queue_drops) {
    Harness h;
    CHECK(h.processor.init(8, 2) == S_OK);
    for (int i = 0; i < 3; ++i) h.processor.run_frame();
    CHECK(h.processor.pending_messages() == 2);
    CHECK(h.processor.dropped_messages() == 1);
}

TEST(acphal_title_memory_reads_are_checked) {
    std::uint32_t value = 7, out = 0;
    CHECK(copy_from_title(&value, &out, sizeof(out)) && out == 7);
    CHECK(!copy_from_title(nullptr, &out, sizeof(out)));
    CHECK(!copy_from_title(reinterpret_cast<const void*>(0x10), &out, sizeof(out)));
    CHECK(!readable(reinterpret_cast<const void*>(~std::uintptr_t(0) - 2), 8));
}
