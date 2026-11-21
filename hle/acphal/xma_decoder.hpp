// SPDX-FileCopyrightText: 2026 gookie and g6shim contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace g6shim::acphal {

constexpr std::uint32_t kXmaFrameSamples = 512;
constexpr unsigned kMaxChannels = 2;

struct PcmPlanes {
    std::vector<float> channel[kMaxChannels];
};

class XmaDecoder {
public:
    virtual ~XmaDecoder() = default;
    virtual void reset() = 0;
    virtual bool decode(const std::uint8_t* packet, unsigned channels, std::uint32_t rate, PcmPlanes& out) = 0;
};

std::uint32_t xma_packet_frames(const std::uint8_t* packet);

class SilentDecoder final : public XmaDecoder {
public:
    void reset() override {}
    bool decode(const std::uint8_t* packet, unsigned channels, std::uint32_t rate, PcmPlanes& out) override;
};

std::unique_ptr<XmaDecoder> make_ffmpeg_decoder();
std::unique_ptr<XmaDecoder> make_xma_decoder();

}  // namespace g6shim::acphal
