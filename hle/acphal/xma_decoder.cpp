// SPDX-FileCopyrightText: 2026 gookie and g6shim contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "xma_decoder.hpp"

#include <g6shim/log.hpp>

#include <atomic>

namespace g6shim::acphal {

std::uint32_t xma_packet_frames(const std::uint8_t* packet) { return packet[0] >> 2; }

bool SilentDecoder::decode(const std::uint8_t* packet, unsigned channels, std::uint32_t, PcmPlanes& out) {
    const std::size_t samples = std::size_t(xma_packet_frames(packet)) * kXmaFrameSamples;
    for (unsigned c = 0; c < channels && c < kMaxChannels; ++c) out.channel[c].resize(out.channel[c].size() + samples);
    return true;
}

std::unique_ptr<XmaDecoder> make_xma_decoder() {
    if (auto decoder = make_ffmpeg_decoder()) return decoder;
    static std::atomic<bool> logged{false};
    if (!logged.exchange(true)) G6SHIM_WARN("acphal: FFmpeg not available, XMA voices play silence");
    return std::make_unique<SilentDecoder>();
}

}  // namespace g6shim::acphal
