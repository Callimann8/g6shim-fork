// SPDX-FileCopyrightText: 2026 gookie and g6shim contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "check.hpp"

#include "apu_memory.hpp"
#include "engine.hpp"
#include "processor.hpp"
#include "shape.hpp"
#include "title_memory.hpp"

#include <cstring>
#include <memory>
#include <string>
#include <utility>
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

struct RecordingEngine final : Engine {
    std::vector<std::string> calls;
    void run_list(std::uint32_t a, std::uint32_t n) override { calls.push_back("list " + std::to_string(a) + " " + std::to_string(n)); }
    void reset_voice(std::uint32_t x) override { calls.push_back("reset " + std::to_string(x)); }
    void refill_input(std::uint32_t x, std::uint32_t b) override { calls.push_back("refill " + std::to_string(x) + " " + std::to_string(b)); }
    void release_dma(std::uint32_t d, std::uint32_t n) override { calls.push_back("release " + std::to_string(d) + " " + std::to_string(n)); }
};

CommandRecord record(Command c, std::uint64_t seq, std::uint32_t p0 = 0, std::uint32_t p1 = 0) {
    CommandRecord r{};
    r.command = c;
    r.sequence = seq;
    r.payload[0] = p0;
    r.payload[1] = p1;
    return r;
}

std::vector<std::pair<std::uint32_t, std::uint64_t>> drain(Processor& p) {
    std::vector<std::pair<std::uint32_t, std::uint64_t>> out;
    Message m{};
    while (p.next_message(m)) out.emplace_back(m.type, m.value);
    return out;
}

constexpr std::uint32_t kListDone = static_cast<std::uint32_t>(MessageType::ListDone);
constexpr std::uint32_t kDone = static_cast<std::uint32_t>(MessageType::CommandDone);
constexpr std::uint32_t kShutdown = static_cast<std::uint32_t>(MessageType::ShutdownDone);

struct FakeMemory final : ApuMemory {
    static constexpr std::uint32_t kBase = 0x10000;
    std::vector<std::uint8_t> bytes = std::vector<std::uint8_t>(0x10000);
    void* map(std::uint32_t a, std::size_t n) override {
        if (a < kBase || a - kBase > bytes.size() || n > bytes.size() - (a - kBase)) return nullptr;
        return bytes.data() + (a - kBase);
    }
    template <typename T>
    T* at(std::uint32_t a) { return reinterpret_cast<T*>(bytes.data() + (a - kBase)); }
};

struct RampDecoder final : XmaDecoder {
    float next = 0.0f;
    void reset() override { next = 0.0f; }
    bool decode(const std::uint8_t* packet, unsigned channels, std::uint32_t, PcmPlanes& out) override {
        const std::size_t n = std::size_t(xma_packet_frames(packet)) * kXmaFrameSamples;
        for (std::size_t i = 0; i < n; ++i) {
            for (unsigned c = 0; c < channels; ++c) out.channel[c].push_back(next + 10000.0f * c);
            next += 1.0f;
        }
        return true;
    }
};

struct EngineRig {
    static constexpr std::uint32_t kList = 0x10000, kInput = 0x11000, kDmaOut = 0x14000;
    FakeMemory memory;
    XmaContext xma[2]{};
    SrcContext src[2]{};
    DmaContext dma[4]{};
    AudioEngine engine{memory, [this] { return ContextArrays{xma, 2, src, 2, dma, 4}; },
                       [] { return std::make_unique<RampDecoder>(); }};

    EngineRig() {
        auto& x = xma[1];
        x.dw[0] = 2 | (1u << 20) | (1u << 21);
        x.dw[1] = 2 | (3u << 27);
        x.dw[2] = 0x20;
        x.dw[5] = kInput;
        x.dw[6] = kInput + 0x1000;
        for (std::uint32_t p = 0; p < 4; ++p) memory.bytes[kInput - FakeMemory::kBase + p * kXmaPacketBytes] = 1 << 2;
        src[1].dw[1] = 1u << kSrcRatioFractionBits;
        dma[3].dw[2] = kDmaOut;
        dma[3].dw[3] = 3;
        auto* list = memory.at<std::uint64_t>(kList);
        list[0] = 0x802001ull | (5ull << 33);
        list[1] = 2ull | (1ull << 13) | (5ull << 25) | (5ull << 38);
        list[2] = 0x10ull | (5ull << 14) | (3ull << 27);
    }

    const float* block(std::uint32_t i) { return memory.at<float>(kDmaOut + i * kDmaBlockBytes); }
};

}  // namespace

TEST(acphal_bounded_queue_order_and_capacity) {
    BoundedQueue<int> q(2);
    CHECK(q.push(1) && q.push(2) && !q.push(3));
    CHECK(q.pop() == 1);
    CHECK(q.push(3));
    CHECK(q.pop() == 2 && q.pop() == 3 && !q.pop());
}

TEST(acphal_processor_requires_init) {
    RecordingEngine e;
    Processor p(e);
    CHECK(p.send(record(Command::RunList, 0)) == E_UNEXPECTED);
    CHECK(p.request_shutdown() == E_UNEXPECTED);
    CHECK(p.init(0, 4) == E_INVALIDARG);
    CHECK(p.init(4, kMaxQueueEntries + 1) == E_INVALIDARG);
    CHECK(p.init(0x400, 0x400) == S_OK);
    CHECK(p.init(0x400, 0x400) == E_UNEXPECTED);
}

TEST(acphal_processor_runs_commands_in_order_on_frames) {
    RecordingEngine e;
    Processor p(e);
    CHECK(p.init(16, 64) == S_OK);
    CHECK(p.send(record(Command::Start, 0, 0xFE)) == S_OK);
    CHECK(p.send(record(Command::ResetVoice, 1, 3)) == S_OK);
    CHECK(p.send(record(Command::RefillInput, 2, 3, 1)) == S_OK);
    CHECK(p.send(record(Command::RunList, 0, 5, 0x1000)) == S_OK);
    CHECK(p.send(record(Command::ReleaseDma, 3, 7, 4)) == S_OK);
    CHECK(e.calls.empty() && p.pending_messages() == 0);
    p.run_frame();
    CHECK((e.calls == std::vector<std::string>{"reset 3", "refill 3 1", "list 4096 5", "release 7 4"}));
    const auto m = drain(p);
    CHECK((m == std::vector<std::pair<std::uint32_t, std::uint64_t>>{{kDone, 1}, {kDone, 2}, {kListDone, 0}, {kDone, 3}}));
}

TEST(acphal_processor_unsequenced_commands_post_no_fence) {
    RecordingEngine e;
    Processor p(e);
    CHECK(p.init(16, 64) == S_OK);
    for (int i = 0; i < 4; ++i) CHECK(p.send(record(Command::RunList, 0)) == S_OK);
    for (int i = 0; i < 3; ++i) p.run_frame();
    const auto m = drain(p);
    CHECK(m.size() == 4);
    for (const auto& x : m) CHECK(x.first == kListDone);
}

TEST(acphal_processor_shutdown_flushes_then_reports) {
    RecordingEngine e;
    Processor p(e);
    CHECK(p.init(8, 64) == S_OK);
    CHECK(p.send(record(Command::ReleaseDma, 9, 0, 4)) == S_OK);
    CHECK(p.request_shutdown() == S_OK);
    const auto m = drain(p);
    CHECK((m == std::vector<std::pair<std::uint32_t, std::uint64_t>>{{kDone, 9}, {kShutdown, 0}}));
}

TEST(acphal_processor_full_queues_fail_or_drop) {
    RecordingEngine e;
    Processor p(e);
    CHECK(p.init(1, 1) == S_OK);
    CHECK(p.send(record(Command::ResetVoice, 1)) == S_OK);
    CHECK(p.send(record(Command::ResetVoice, 2)) == E_OUTOFMEMORY);
    p.run_frame();
    CHECK(p.send(record(Command::ResetVoice, 3)) == S_OK);
    p.run_frame();
    CHECK(p.pending_messages() == 1 && p.dropped_messages() == 1);
}

TEST(acphal_commands_have_known_payload_sizes) {
    CHECK(known_command(1) && known_command(2) && known_command(7) && known_command(0x10) && known_command(0x13));
    CHECK(!known_command(0) && !known_command(3) && !known_command(0x11));
    CHECK(payload_bytes(Command::RunList) == 16);
}

TEST(acphal_context_fields_decode) {
    XmaContext x{};
    x.dw[0] = 2 | (1u << 21);
    x.dw[1] = 2 | (2u << 27) | (1u << 29);
    CHECK(xma_packets(x, 0) == 2 && xma_packets(x, 1) == 2);
    CHECK(!xma_valid(x, 0) && xma_valid(x, 1));
    CHECK(xma_rate(x) == 44100 && xma_channels(x) == 2);
    SrcContext s{};
    s.dw[1] = (1u << 18) / 2 | (1u << 22);
    CHECK(src_ratio(s) == 0.5);
    DmaContext d{};
    d.dw[2] = 0x12345678;
    d.dw[3] = 3;
    CHECK(dma_address(d) == 0x12345600 && dma_blocks(d) == 4);
    dma_set_state(d, 1, 3, true);
    CHECK(dma_read(d) == 1 && dma_write(d) == 3 && dma_full(d));
    const ListEntry e{2ull | (7ull << 13) | (9ull << 25) | (10ull << 38)};
    CHECK(e.op() == ListOp::Xma && e.xma_index() == 7 && e.xma_out(0) == 9 && e.xma_out(1) == 10);
    const ListEntry f{0x10ull | (9ull << 14) | (300ull << 27)};
    CHECK(f.op() == ListOp::Dma && f.dma_source() == 9 && f.dma_index() == 300);
}

TEST(acphal_engine_decodes_into_dma_ring) {
    EngineRig rig;
    for (int i = 0; i < 4; ++i) rig.engine.run_list(EngineRig::kList, 3);
    CHECK(dma_full(rig.dma[3]) && dma_write(rig.dma[3]) == 0);
    for (std::uint32_t b = 0; b < 4; ++b) CHECK(rig.block(b)[0] == static_cast<float>(b * kDmaBlockSamples));
    CHECK(rig.block(3)[kDmaBlockSamples - 1] == static_cast<float>(4 * kDmaBlockSamples - 1));
    rig.engine.run_list(EngineRig::kList, 3);
    CHECK(rig.block(0)[0] == 0.0f);
    rig.engine.release_dma(3, 4);
    CHECK(!dma_full(rig.dma[3]) && dma_read(rig.dma[3]) == 0);
    rig.engine.run_list(EngineRig::kList, 3);
    CHECK(rig.block(0)[0] == static_cast<float>(5 * kDmaBlockSamples));
}

TEST(acphal_engine_consumes_and_refills_input_buffers) {
    EngineRig rig;
    for (int i = 0; i < 4; ++i) rig.engine.run_list(EngineRig::kList, 3);
    CHECK(!xma_valid(rig.xma[1], 0) && xma_valid(rig.xma[1], 1));
    rig.engine.refill_input(1, 0);
    CHECK(xma_valid(rig.xma[1], 0));
    rig.engine.refill_input(1, 2);
    rig.engine.refill_input(99, 0);
}

TEST(acphal_engine_applies_src_ratio) {
    EngineRig rig;
    rig.src[1].dw[1] = 2u << kSrcRatioFractionBits;
    rig.engine.run_list(EngineRig::kList, 3);
    CHECK(rig.block(0)[1] == 2.0f && rig.block(0)[127] == 254.0f);
}

TEST(acphal_engine_starves_to_silence_and_rejects_bad_lists) {
    EngineRig rig;
    rig.xma[1].dw[0] &= ~((1u << 20) | (1u << 21));
    rig.engine.run_list(EngineRig::kList, 3);
    CHECK(dma_write(rig.dma[3]) == 1 && rig.block(0)[5] == 0.0f);
    rig.engine.run_list(0x90000, 3);
    rig.engine.run_list(EngineRig::kList, kMaxListEntries + 1);
    rig.engine.reset_voice(1000);
    rig.engine.release_dma(1000, 4);
    CHECK(dma_write(rig.dma[3]) == 1);
}

TEST(acphal_silent_decoder_keeps_input_rate) {
    SilentDecoder d;
    PcmPlanes out;
    std::uint8_t packet[kXmaPacketBytes] = {static_cast<std::uint8_t>(3 << 2)};
    CHECK(d.decode(packet, 2, 48000, out));
    CHECK(out.channel[0].size() == 3 * kXmaFrameSamples && out.channel[1].size() == 3 * kXmaFrameSamples);
}

TEST(acphal_decoder_falls_back_to_silence_without_ffmpeg) {
    auto d = make_xma_decoder();
    CHECK(d != nullptr);
    if (!make_ffmpeg_decoder()) CHECK(dynamic_cast<SilentDecoder*>(d.get()) != nullptr);
}

TEST(acphal_title_memory_reads_are_checked) {
    std::uint32_t value = 7, out = 0;
    CHECK(copy_from_title(&value, &out, sizeof(out)) && out == 7);
    CHECK(!copy_from_title(nullptr, &out, sizeof(out)));
    CHECK(!copy_from_title(reinterpret_cast<const void*>(0x10), &out, sizeof(out)));
    CHECK(!readable(reinterpret_cast<const void*>(~std::uintptr_t(0) - 2), 8));
}
