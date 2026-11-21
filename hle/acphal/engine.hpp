// SPDX-FileCopyrightText: 2026 gookie and g6shim contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "contexts.hpp"
#include "processor.hpp"
#include "xma_decoder.hpp"

#include <array>
#include <cstddef>
#include <functional>
#include <map>
#include <memory>

namespace g6shim::acphal {

class ApuMemory {
public:
    virtual void* map(std::uint32_t apu_address, std::size_t bytes) = 0;

protected:
    ~ApuMemory() = default;
};

struct ContextArrays {
    XmaContext* xma = nullptr;
    std::uint32_t xma_count = 0;
    SrcContext* src = nullptr;
    std::uint32_t src_count = 0;
    DmaContext* dma = nullptr;
    std::uint32_t dma_count = 0;
};

using Block = std::array<float, kDmaBlockSamples>;
using DecoderFactory = std::function<std::unique_ptr<XmaDecoder>()>;

constexpr double kMinPitch = 0.0625;
constexpr double kMaxPitch = 4.0;

class AudioEngine final : public Engine {
public:
    AudioEngine(ApuMemory& memory, std::function<ContextArrays()> contexts, DecoderFactory decoders);

    void run_list(std::uint32_t apu_address, std::uint32_t entries) override;
    void reset_voice(std::uint32_t xma_index) override;
    void refill_input(std::uint32_t xma_index, std::uint32_t buffer) override;
    void release_dma(std::uint32_t dma_index, std::uint32_t blocks) override;

private:
    struct Voice {
        std::unique_ptr<XmaDecoder> decoder;
        bool positioned = false;
        unsigned buffer = 0;
        std::uint32_t packet = 0;
        PcmPlanes pcm;
        double phase = 0.0;
    };

    Voice& voice(std::uint32_t xma_index);
    bool decode_next(Voice& v, XmaContext& ctx);
    void render_voice(std::uint32_t xma_index, Block (&out)[kMaxChannels]);
    void write_dma(std::uint32_t dma_index, const Block& block);
    double pitch(std::uint32_t xma_index) const;

    ApuMemory& memory_;
    std::function<ContextArrays()> contexts_;
    DecoderFactory decoders_;
    ContextArrays arrays_;
    std::map<std::uint32_t, Voice> voices_;
    std::map<std::uint32_t, Block> scratch_;
};

class TitleApuMemory final : public ApuMemory {
public:
    void* map(std::uint32_t apu_address, std::size_t bytes) override;
};

}  // namespace g6shim::acphal
