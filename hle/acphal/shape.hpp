// SPDX-FileCopyrightText: 2026 gookie and g6shim contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <windows.h>

#include <array>
#include <cstdint>

namespace g6shim::acphal {

enum class ShapeKind : std::uint32_t { Src, EqCompressor, FilterVolume, Dma, Xma, Pcm, Count };

constexpr std::size_t kShapeKinds = static_cast<std::size_t>(ShapeKind::Count);
constexpr std::array<std::uint32_t, kShapeKinds> kShapeStride = {32, 160, 32, 16, 64, 32};
constexpr std::uint32_t kMaxShapeCount = 0x10000;

struct ShapeRequest {
    std::uint32_t count[kShapeKinds];
    void* array[kShapeKinds];
};
static_assert(sizeof(ShapeRequest) == 0x48);

struct ShapeArrays {
    std::array<std::uint32_t, kShapeKinds> count{};
    std::array<void*, kShapeKinds> array{};
};

HRESULT allocate_shapes(ShapeRequest& request);
HRESULT release_shapes();
ShapeArrays current_shapes();

}  // namespace g6shim::acphal
