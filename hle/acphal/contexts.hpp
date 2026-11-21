// SPDX-FileCopyrightText: 2026 gookie and g6shim contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <cstdint>

namespace g6shim::acphal {

constexpr std::uint32_t bits(std::uint32_t value, unsigned low, unsigned count) {
    return (value >> low) & ((1u << count) - 1u);
}

constexpr std::uint64_t bits64(std::uint64_t value, unsigned low, unsigned count) {
    return (value >> low) & ((1ull << count) - 1ull);
}

struct XmaContext {
    std::uint32_t dw[16];
};
static_assert(sizeof(XmaContext) == 64);

constexpr unsigned kXmaBuffers = 2;
constexpr std::uint32_t kXmaPacketBytes = 2048;
constexpr std::uint32_t kXmaPacketBits = kXmaPacketBytes * 8;
constexpr unsigned kXmaValidBit = 20;
constexpr unsigned kXmaInputAddressDword = 5;
constexpr unsigned kXmaReadOffsetDword = 2;
constexpr std::uint32_t kXmaRateCodes[4] = {24000, 32000, 44100, 48000};

inline std::uint32_t xma_packets(const XmaContext& c, unsigned buffer) { return bits(c.dw[buffer], 0, 12); }
inline bool xma_valid(const XmaContext& c, unsigned buffer) { return bits(c.dw[0], kXmaValidBit + buffer, 1) != 0; }
inline void xma_set_valid(XmaContext& c, unsigned buffer, bool valid) {
    const std::uint32_t mask = 1u << (kXmaValidBit + buffer);
    c.dw[0] = valid ? (c.dw[0] | mask) : (c.dw[0] & ~mask);
}
inline std::uint32_t xma_rate(const XmaContext& c) { return kXmaRateCodes[bits(c.dw[1], 27, 2)]; }
inline unsigned xma_channels(const XmaContext& c) { return bits(c.dw[1], 29, 1) ? 2u : 1u; }
inline std::uint32_t xma_read_offset(const XmaContext& c) { return bits(c.dw[kXmaReadOffsetDword], 0, 26); }
inline void xma_set_read_offset(XmaContext& c, std::uint32_t offset) {
    c.dw[kXmaReadOffsetDword] = (c.dw[kXmaReadOffsetDword] & ~0x3FFFFFFu) | (offset & 0x3FFFFFFu);
}
inline std::uint32_t xma_input_address(const XmaContext& c, unsigned buffer) {
    return c.dw[kXmaInputAddressDword + buffer];
}

struct SrcContext {
    std::uint32_t dw[8];
};
static_assert(sizeof(SrcContext) == 32);

constexpr unsigned kSrcRatioFractionBits = 18;

inline double src_ratio(const SrcContext& c) {
    return static_cast<double>(bits(c.dw[1], 0, 21)) / static_cast<double>(1u << kSrcRatioFractionBits);
}

struct DmaContext {
    std::uint32_t dw[4];
};
static_assert(sizeof(DmaContext) == 16);

constexpr std::uint32_t kDmaBlockBytes = 0x200;
constexpr std::uint32_t kDmaBlockSamples = kDmaBlockBytes / sizeof(float);
constexpr unsigned kDmaFullBit = 16;

inline std::uint32_t dma_address(const DmaContext& c) { return c.dw[2] & ~(kDmaBlockBytes - 1); }
constexpr std::uint32_t kDmaMaxBlocks = 16;

inline std::uint32_t dma_blocks(const DmaContext& c) {
    const std::uint32_t code = bits(c.dw[3], 0, 5);
    return code == 0 || code > 5 ? 1u : 1u << (code - 1u);
}
inline std::uint32_t dma_read(const DmaContext& c) { return bits(c.dw[1], 0, 5); }
inline std::uint32_t dma_write(const DmaContext& c) { return bits(c.dw[1], 8, 5); }
inline bool dma_full(const DmaContext& c) { return bits(c.dw[1], kDmaFullBit, 1) != 0; }
inline void dma_set_state(DmaContext& c, std::uint32_t read, std::uint32_t write, bool full) {
    c.dw[1] = (read & 0x1F) | ((write & 0x1F) << 8) | (full ? 1u << kDmaFullBit : 0u);
}

enum class ListOp : std::uint32_t { Xma = 2, Dma = 0x10 };

struct ListEntry {
    std::uint64_t raw;
    ListOp op() const { return static_cast<ListOp>(bits64(raw, 0, 5)); }
    std::uint32_t xma_index() const { return static_cast<std::uint32_t>(bits64(raw, 13, 12)); }
    std::uint32_t xma_out(unsigned channel) const {
        return static_cast<std::uint32_t>(bits64(raw, channel ? 38 : 25, 13));
    }
    std::uint32_t dma_source() const { return static_cast<std::uint32_t>(bits64(raw, 14, 13)); }
    std::uint32_t dma_index() const { return static_cast<std::uint32_t>(bits64(raw, 27, 12)); }
};

constexpr std::uint32_t kMaxListEntries = 0x600;

}  // namespace g6shim::acphal
