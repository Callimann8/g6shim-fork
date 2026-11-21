// SPDX-FileCopyrightText: 2026 gookie and g6shim contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "engine.hpp"

#include "apu_memory.hpp"
#include "title_memory.hpp"

#include <g6shim/log.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <utility>

namespace g6shim::acphal {

namespace {

void warn_once(std::atomic<bool>& flag, const char* text, std::uint32_t value) {
    if (!flag.exchange(true)) G6SHIM_WARN("acphal: %s (0x%x)", text, value);
}

std::atomic<bool> g_bad_list{false}, g_bad_xma{false}, g_bad_dma{false}, g_overrun{false}, g_bad_input{false};

void set_valid(XmaContext& ctx, unsigned buffer, bool valid) {
    std::atomic_ref<std::uint32_t> dw0(ctx.dw[0]);
    const std::uint32_t mask = 1u << (kXmaValidBit + buffer);
    if (valid)
        dw0.fetch_or(mask);
    else
        dw0.fetch_and(~mask);
}

void erase_front(std::vector<float>& v, std::size_t n) {
    v.erase(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(std::min(n, v.size())));
}

}  // namespace

void* TitleApuMemory::map(std::uint32_t apu_address, std::size_t bytes) {
    void* p = reinterpret_cast<void*>(static_cast<std::uintptr_t>(apu_address));
    if (apu_address == 0) return nullptr;
    if (apu_blocks().contains(apu_address, bytes) || readable(p, bytes)) return p;
    return nullptr;
}

AudioEngine::AudioEngine(ApuMemory& memory, std::function<ContextArrays()> contexts, DecoderFactory decoders)
    : memory_(memory), contexts_(std::move(contexts)), decoders_(std::move(decoders)) {}

AudioEngine::Voice& AudioEngine::voice(std::uint32_t xma_index) {
    auto& v = voices_[xma_index];
    if (!v.decoder) v.decoder = decoders_();
    return v;
}

double AudioEngine::pitch(std::uint32_t xma_index) const {
    if (xma_index >= arrays_.src_count) return 1.0;
    const double ratio = src_ratio(arrays_.src[xma_index]);
    return ratio == 0.0 ? 1.0 : std::clamp(ratio, kMinPitch, kMaxPitch);
}

void AudioEngine::reset_voice(std::uint32_t xma_index) {
    arrays_ = contexts_();
    if (xma_index >= arrays_.xma_count) return warn_once(g_bad_xma, "XMA context index out of range", xma_index);
    auto& v = voice(xma_index);
    v.decoder->reset();
    v.positioned = false;
    v.pcm = {};
    v.phase = 0.0;
}

void AudioEngine::refill_input(std::uint32_t xma_index, std::uint32_t buffer) {
    arrays_ = contexts_();
    if (xma_index >= arrays_.xma_count || buffer >= kXmaBuffers)
        return warn_once(g_bad_xma, "bad input refill", xma_index);
    set_valid(arrays_.xma[xma_index], buffer, true);
}

void AudioEngine::release_dma(std::uint32_t dma_index, std::uint32_t blocks) {
    arrays_ = contexts_();
    if (dma_index >= arrays_.dma_count) return warn_once(g_bad_dma, "DMA context index out of range", dma_index);
    auto& ctx = arrays_.dma[dma_index];
    const std::uint32_t size = dma_blocks(ctx);
    const std::uint32_t used = dma_full(ctx) ? size : (dma_write(ctx) + size - dma_read(ctx)) % size;
    const std::uint32_t n = std::min(blocks, used);
    dma_set_state(ctx, (dma_read(ctx) + n) % size, dma_write(ctx), dma_full(ctx) && n == 0);
}

bool AudioEngine::decode_next(Voice& v, XmaContext& ctx) {
    if (!v.positioned) {
        std::uint32_t packet = xma_read_offset(ctx) / kXmaPacketBits;
        const std::uint32_t first = xma_packets(ctx, 0);
        v.buffer = packet >= first ? 1u : 0u;
        v.packet = v.buffer ? packet - first : packet;
        v.positioned = true;
    }
    for (unsigned attempt = 0; attempt < kXmaBuffers; ++attempt) {
        if (!xma_valid(ctx, v.buffer)) return false;
        if (v.packet < xma_packets(ctx, v.buffer)) break;
        set_valid(ctx, v.buffer, false);
        v.buffer ^= 1u;
        v.packet = 0;
    }
    if (!xma_valid(ctx, v.buffer) || v.packet >= xma_packets(ctx, v.buffer)) return false;
    const auto address = xma_input_address(ctx, v.buffer) + v.packet * kXmaPacketBytes;
    const auto* packet = static_cast<const std::uint8_t*>(memory_.map(address, kXmaPacketBytes));
    if (!packet) {
        warn_once(g_bad_input, "XMA input buffer is not APU memory", address);
        return false;
    }
    const bool ok = v.decoder->decode(packet, xma_channels(ctx), xma_rate(ctx), v.pcm);
    if (++v.packet >= xma_packets(ctx, v.buffer)) {
        set_valid(ctx, v.buffer, false);
        v.buffer ^= 1u;
        v.packet = 0;
    }
    return ok;
}

void AudioEngine::render_voice(std::uint32_t xma_index, Block (&out)[kMaxChannels]) {
    for (auto& b : out) b.fill(0.0f);
    if (xma_index >= arrays_.xma_count) return warn_once(g_bad_xma, "XMA context index out of range", xma_index);
    auto& ctx = arrays_.xma[xma_index];
    auto& v = voice(xma_index);
    const unsigned channels = xma_channels(ctx);
    const double ratio = pitch(xma_index);
    const auto needed = static_cast<std::size_t>(std::ceil(v.phase + kDmaBlockSamples * ratio)) + 1;
    while (v.pcm.channel[0].size() < needed && decode_next(v, ctx)) {
    }
    const auto available = v.pcm.channel[0].size();
    for (unsigned c = 0; c < channels; ++c) {
        const auto& in = v.pcm.channel[c];
        for (std::uint32_t i = 0; i < kDmaBlockSamples; ++i) {
            const double pos = v.phase + i * ratio;
            const auto j = static_cast<std::size_t>(pos);
            if (j + 1 >= in.size()) break;
            const auto frac = static_cast<float>(pos - static_cast<double>(j));
            out[c][i] = in[j] + (in[j + 1] - in[j]) * frac;
        }
    }
    const double end = v.phase + kDmaBlockSamples * ratio;
    const auto consumed = std::min(static_cast<std::size_t>(end), available);
    for (auto& plane : v.pcm.channel) erase_front(plane, consumed);
    v.phase = available >= static_cast<std::size_t>(end) ? end - std::floor(end) : 0.0;
}

void AudioEngine::write_dma(std::uint32_t dma_index, const Block& block) {
    if (dma_index >= arrays_.dma_count) return warn_once(g_bad_dma, "DMA context index out of range", dma_index);
    auto& ctx = arrays_.dma[dma_index];
    if (dma_full(ctx)) return warn_once(g_overrun, "DMA ring full, block dropped", dma_index);
    const std::uint32_t size = dma_blocks(ctx);
    const std::uint32_t write = dma_write(ctx) % size;
    void* target = memory_.map(dma_address(ctx) + write * kDmaBlockBytes, kDmaBlockBytes);
    if (!target) return warn_once(g_bad_dma, "DMA target is not APU memory", dma_address(ctx));
    std::memcpy(target, block.data(), kDmaBlockBytes);
    const std::uint32_t next = (write + 1) % size;
    dma_set_state(ctx, dma_read(ctx), next, next == dma_read(ctx));
}

void AudioEngine::run_list(std::uint32_t apu_address, std::uint32_t entries) {
    arrays_ = contexts_();
    if (entries == 0) return;
    if (entries > kMaxListEntries) return warn_once(g_bad_list, "command list too long", entries);
    const auto* list = static_cast<const ListEntry*>(memory_.map(apu_address, entries * sizeof(ListEntry)));
    if (!list) return warn_once(g_bad_list, "command list is not APU memory", apu_address);
    scratch_.clear();
    for (std::uint32_t i = 0; i < entries; ++i) {
        const ListEntry e = list[i];
        if (e.op() == ListOp::Xma) {
            Block out[kMaxChannels];
            render_voice(e.xma_index(), out);
            scratch_[e.xma_out(0)] = out[0];
            if (e.xma_out(1) != e.xma_out(0)) scratch_[e.xma_out(1)] = out[1];
        } else if (e.op() == ListOp::Dma) {
            auto it = scratch_.find(e.dma_source());
            write_dma(e.dma_index(), it != scratch_.end() ? it->second : Block{});
        }
    }
}

}  // namespace g6shim::acphal
